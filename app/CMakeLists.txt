file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

add_executable(app ${APP_SOURCES})

target_link_libraries(app PRIVATE engine)
