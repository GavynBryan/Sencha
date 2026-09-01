# sencha-component-codegen
#
# A standalone host tool: it reads component annotations and writes the
# ComponentDefinition companions the engine projects from. It links no engine
# code, so it can be built and run before anything else.
#
# Clang development packages are needed to BUILD Sencha, never to consume it:
# the tool is installed as part of the SDK so an out-of-tree game module runs
# the prebuilt binary and needs no LLVM of its own.
#
# While no component is annotated the tool is optional, which is what lets the
# foundation land before the toolchain is updated everywhere. The first
# annotated header makes it required, and sencha_generate_component_metadata
# fails loudly if it is missing.

find_package(Clang QUIET CONFIG)

if(NOT Clang_FOUND)
    set(SENCHA_COMPONENT_CODEGEN_AVAILABLE OFF CACHE INTERNAL "")
    message(STATUS
        "sencha-component-codegen: Clang development package not found -- component "
        "metadata generation is disabled. Install clang-devel/llvm-devel (or set "
        "Clang_DIR) to build it. Required once any component is annotated.")
    return()
endif()

set(SENCHA_COMPONENT_CODEGEN_AVAILABLE ON CACHE INTERNAL "")

add_executable(sencha-component-codegen ComponentCodegen.cpp)

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
