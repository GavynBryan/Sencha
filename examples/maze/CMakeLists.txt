file(GLOB_RECURSE MAZE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

file(GLOB_RECURSE MAZE_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)

add_executable(maze ${MAZE_SOURCES} ${MAZE_HEADERS})

target_include_directories(maze PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(maze PRIVATE infuser)
