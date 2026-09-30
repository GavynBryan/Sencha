file(GLOB_RECURSE PROJECT_BROWSER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

file(GLOB_RECURSE PROJECT_BROWSER_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

# The project workspace, linked by the application that hosts it.
add_library(project_browser STATIC
    ${PROJECT_BROWSER_SOURCES}
    ${PROJECT_BROWSER_HEADERS}
)

target_link_libraries(project_browser PUBLIC editor_common)
sencha_warnings(project_browser)
target_include_directories(project_browser PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
