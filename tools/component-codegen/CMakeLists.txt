# sencha-component-codegen
#
# A standalone host tool: it reads component annotations and writes the
# ComponentDefinition companions the engine projects from. It links no engine
# code, so it can be built and run before anything else.
#
# Clang development packages are needed to BUILD Sencha, never to consume it:
# the tool is installed with the SDK, so an out-of-tree game module runs the
# prebuilt binary and needs no LLVM of its own.
#
# A host whose engine toolchain has no Clang libraries -- MSVC, or a
# cross-compile -- configures this directory as its own project first and hands
# the binary to the engine build as SENCHA_COMPONENT_CODEGEN_EXECUTABLE.

cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(SenchaComponentCodegen LANGUAGES CXX)
    include(GNUInstallDirs)
endif()

set(SENCHA_COMPONENT_CODEGEN_EXECUTABLE "" CACHE FILEPATH
    "A prebuilt sencha-component-codegen to use instead of building the tool")

if(SENCHA_COMPONENT_CODEGEN_EXECUTABLE)
    add_executable(sencha::component-codegen IMPORTED GLOBAL)
    set_target_properties(sencha::component-codegen PROPERTIES
        IMPORTED_LOCATION "${SENCHA_COMPONENT_CODEGEN_EXECUTABLE}")
    install(PROGRAMS "${SENCHA_COMPONENT_CODEGEN_EXECUTABLE}"
        DESTINATION "${CMAKE_INSTALL_BINDIR}"
        RENAME "sencha-component-codegen${CMAKE_EXECUTABLE_SUFFIX}")
    return()
endif()

# LLVMConfig probes for FFI with check_c_source_compiles, and the engine only
# enables CXX.
enable_language(C)

find_package(Clang QUIET CONFIG)
if(NOT Clang_FOUND)
    message(FATAL_ERROR
        "sencha-component-codegen: Clang development package not found. Every "
        "component's metadata is generated from its header, so the engine cannot be "
        "built without it. Install clang-devel/llvm-devel (Fedora) or "
        "clang/libclang-dev/llvm-dev (Debian), or set Clang_DIR.")
endif()

# Asked of clang rather than derived from LLVM_LIBRARY_DIR, which points at
# lib64 while the resource headers live under lib on this distribution. A
# shipped generator can be pointed elsewhere with --resource-dir.
execute_process(
    COMMAND "${LLVM_TOOLS_BINARY_DIR}/clang${CMAKE_EXECUTABLE_SUFFIX}" -print-resource-dir
    OUTPUT_VARIABLE SENCHA_CLANG_RESOURCE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _resource_result)
if(NOT _resource_result EQUAL 0)
    message(FATAL_ERROR "sencha-component-codegen: could not determine clang's resource directory")
endif()
# Clang prints a native path, and a backslash would be an escape inside the
# string literal it is compiled into.
file(TO_CMAKE_PATH "${SENCHA_CLANG_RESOURCE_DIR}" SENCHA_CLANG_RESOURCE_DIR)

add_executable(sencha-component-codegen ComponentCodegen.cpp)
add_executable(sencha::component-codegen ALIAS sencha-component-codegen)

target_compile_definitions(sencha-component-codegen PRIVATE
    SENCHA_CLANG_RESOURCE_DIR="${SENCHA_CLANG_RESOURCE_DIR}")

target_include_directories(sencha-component-codegen SYSTEM PRIVATE
    ${CLANG_INCLUDE_DIRS} ${LLVM_INCLUDE_DIRS})

separate_arguments(_llvm_definitions NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(sencha-component-codegen PRIVATE ${_llvm_definitions})

# A class deriving from one of Clang's must match its RTTI setting, or the
# base's typeinfo is an unresolved symbol at link time. Distribution packages
# enable RTTI; the official release binaries do not.
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(sencha-component-codegen PRIVATE
        $<IF:$<CXX_COMPILER_ID:MSVC>,/GR-,-fno-rtti>)
endif()

# The MSVC flags Clang's own headers are compiled with, plus the object format
# RecursiveASTVisitor's instantiations need.
target_compile_options(sencha-component-codegen PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/bigobj /permissive- /Zc:preprocessor /Zc:__cplusplus>)

# The Windows release records its build machine's DIA SDK path against
# LLVMDebugInfoPDB. Nothing the tool links resolves to that archive, so drop
# the path rather than hunt for a local copy.
if(TARGET LLVMDebugInfoPDB)
    get_target_property(_pdb_libraries LLVMDebugInfoPDB INTERFACE_LINK_LIBRARIES)
    if(_pdb_libraries)
        list(FILTER _pdb_libraries EXCLUDE REGEX "diaguids\\.lib$")
        set_target_properties(LLVMDebugInfoPDB PROPERTIES
            INTERFACE_LINK_LIBRARIES "${_pdb_libraries}")
    endif()
endif()

# The shared clang library where the distribution built one: it keeps the tool
# small and its link fast. The Windows release only has the static archives.
if(TARGET clang-cpp)
    target_link_libraries(sencha-component-codegen PRIVATE clang-cpp LLVM)
else()
    target_link_libraries(sencha-component-codegen PRIVATE clangTooling)
endif()

# The Windows release libraries are built against the release CRT, whatever
# configuration the tool itself is built in.
set_target_properties(sencha-component-codegen PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    MSVC_RUNTIME_LIBRARY MultiThreadedDLL)

# SenchaConfig.cmake imports the installed binary as sencha::component-codegen.
install(TARGETS sencha-component-codegen
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
