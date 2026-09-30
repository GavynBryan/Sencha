file(GLOB_RECURSE PROJECT_BROWSER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
list(REMOVE_ITEM PROJECT_BROWSER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

file(GLOB_RECURSE PROJECT_BROWSER_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

# The project browser as a library, so the application that hosts it links it;
# the executable is only the entry point.
add_library(project_browser STATIC
    ${PROJECT_BROWSER_SOURCES}
    ${PROJECT_BROWSER_HEADERS}
)

target_link_libraries(project_browser PUBLIC editor_common)
sencha_warnings(project_browser)
target_include_directories(project_browser PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

add_executable(kettle src/main.cpp)
target_link_libraries(kettle PRIVATE project_browser)
sencha_warnings(kettle)

set_target_properties(kettle PROPERTIES
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
install(TARGETS kettle RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
