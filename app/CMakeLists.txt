# The host's own command-line policy, on its own so the tests can link it
# without linking a main().
add_library(app_host_arguments STATIC ${CMAKE_CURRENT_SOURCE_DIR}/HostArguments.cpp)
target_include_directories(app_host_arguments PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
sencha_warnings(app_host_arguments)

file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)
list(REMOVE_ITEM APP_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/HostArguments.cpp)

add_executable(app ${APP_SOURCES})

target_link_libraries(app PRIVATE engine app_host_arguments)
sencha_warnings(app)

# Install the runtime host into the SDK (engine-roadmap.md: SDK boundary, shipped). RPATH
# $ORIGIN/../lib so the installed app finds libsencha_engine.so beside it without
# LD_LIBRARY_PATH; this is the bin/ + lib/ install layout.
set_target_properties(app PROPERTIES
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
install(TARGETS app RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
