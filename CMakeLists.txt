cmake_minimum_required(VERSION 3.20)
project(Sencha LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static link the C++ runtime so executables don't depend on MinGW DLLs at runtime.
if(MINGW)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
endif()

add_subdirectory(engine)
add_subdirectory(engine/infuser)
add_subdirectory(app)
add_subdirectory(test)
add_subdirectory(examples/maze)
