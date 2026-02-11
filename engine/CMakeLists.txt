file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/teapot/*.cpp
)

file(GLOB_RECURSE ENGINE_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/kettle/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/teapot/*.h
)

add_library(engine STATIC ${ENGINE_SOURCES} ${ENGINE_HEADERS})

target_include_directories(engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ---------------------------------------------------------------------------
# Infuser layer — optional, requires GLEW + OpenGL
# ---------------------------------------------------------------------------
find_package(OpenGL QUIET)
find_package(GLEW QUIET)

if(OpenGL_FOUND AND GLEW_FOUND)
    file(GLOB_RECURSE INFUSER_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/infuser/*.cpp
    )

    file(GLOB_RECURSE INFUSER_HEADERS CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/include/infuser/*.h
    )

    add_library(infuser STATIC ${INFUSER_SOURCES} ${INFUSER_HEADERS})

    target_link_libraries(infuser PUBLIC engine OpenGL::GL GLEW::GLEW)

    message(STATUS "Sencha: Infuser layer enabled (GLEW + OpenGL found)")
else()
    message(STATUS "Sencha: Infuser layer disabled (GLEW or OpenGL not found)")
endif()
