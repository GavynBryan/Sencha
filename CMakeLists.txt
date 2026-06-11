cmake_minimum_required(VERSION 3.20)
project(Sencha LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

option(SENCHA_ENABLE_TSAN
    "Build with ThreadSanitizer (GCC/Clang only). Used to run the test suites against the job system's concurrent core; see docs/ecs/parallelization.md."
    OFF)

if(SENCHA_ENABLE_TSAN)
    if(MSVC)
        message(FATAL_ERROR "SENCHA_ENABLE_TSAN requires GCC or Clang")
    endif()
    add_compile_options(-fsanitize=thread -g)
    add_link_options(-fsanitize=thread)
endif()

# Static link the C++ runtime so executables don't depend on MinGW DLLs at runtime.
if(MINGW)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
endif()

add_subdirectory(engine)

add_subdirectory(app)
add_subdirectory(example)
add_subdirectory(test)
