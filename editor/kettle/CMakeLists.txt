file(GLOB_RECURSE KETTLE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

file(GLOB_RECURSE KETTLE_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

add_executable(kettle
    ${KETTLE_SOURCES}
    ${KETTLE_HEADERS}
)

target_link_libraries(kettle PRIVATE editor_common)
target_include_directories(kettle PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

set_target_properties(kettle PROPERTIES
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
install(TARGETS kettle RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
