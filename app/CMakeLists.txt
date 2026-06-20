file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

add_executable(app ${APP_SOURCES})

target_link_libraries(app PRIVATE engine)

# Install the runtime host into the SDK (real-engine-roadmap.md Phase 0). RPATH
# $ORIGIN/../lib so the installed app finds libsencha_engine.so beside it without
# LD_LIBRARY_PATH; this is the bin/ + lib/ install layout.
set_target_properties(app PROPERTIES
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
install(TARGETS app RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
