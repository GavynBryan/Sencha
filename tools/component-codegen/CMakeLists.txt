# sencha-component-codegen
#
# A standalone host tool: it reads component annotations and writes the
# ComponentDefinition companions the engine projects from. It links no engine
# code, so it can be built and run before anything else.
#
# Clang development packages are needed to BUILD Sencha, never to consume it:
# the tool is installed as part of the SDK so an out-of-tree game module runs
# the prebuilt binary and needs no LLVM of its own.

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
    COMMAND "${LLVM_TOOLS_BINARY_DIR}/clang" -print-resource-dir
    OUTPUT_VARIABLE SENCHA_CLANG_RESOURCE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _resource_result)
if(NOT _resource_result EQUAL 0)
    message(FATAL_ERROR "sencha-component-codegen: could not determine clang's resource directory")
endif()

add_executable(sencha-component-codegen ComponentCodegen.cpp)

target_compile_definitions(sencha-component-codegen PRIVATE
    SENCHA_CLANG_RESOURCE_DIR="${SENCHA_CLANG_RESOURCE_DIR}")

target_include_directories(sencha-component-codegen SYSTEM PRIVATE
    ${CLANG_INCLUDE_DIRS} ${LLVM_INCLUDE_DIRS})

target_compile_definitions(sencha-component-codegen PRIVATE ${LLVM_DEFINITIONS})

# Link the shared clang library rather than the static component archives: it
# keeps the tool small and its link fast.
target_link_libraries(sencha-component-codegen PRIVATE clang-cpp LLVM)

set_target_properties(sencha-component-codegen PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON)

# Exported so an installed SDK hands out the built binary. An out-of-tree game
# module then generates its own components' metadata with no Clang installed.
install(TARGETS sencha-component-codegen
    EXPORT SenchaTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
