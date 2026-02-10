find_package(OpenGL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
pkg_check_modules(GLEW REQUIRED glew)

file(GLOB_RECURSE INFUSER_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

file(GLOB_RECURSE INFUSER_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
)

add_library(infuser STATIC ${INFUSER_SOURCES} ${INFUSER_HEADERS})

target_include_directories(infuser PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${SDL2_INCLUDE_DIRS}
    ${GLEW_INCLUDE_DIRS}
)

target_link_libraries(infuser PUBLIC
    engine
    ${SDL2_LIBRARIES}
    ${GLEW_LIBRARIES}
    OpenGL::GL
)
