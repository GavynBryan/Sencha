cmake_minimum_required(VERSION 3.20)
project(Sencha LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

# Static link the C++ runtime so executables don't depend on MinGW DLLs at runtime.
if(MINGW)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
endif()

add_subdirectory(Sencha.Core)
add_subdirectory(Sencha.2D)
add_subdirectory(Sencha.3D)

add_library(sencha_engine INTERFACE)
add_library(sencha::engine ALIAS sencha_engine)
target_link_libraries(sencha_engine
    INTERFACE
        sencha_core
        sencha_2d
        sencha_3d
)

add_library(engine INTERFACE)
target_link_libraries(engine INTERFACE sencha_engine)

add_subdirectory(app)
add_subdirectory(example)
add_subdirectory(test)
