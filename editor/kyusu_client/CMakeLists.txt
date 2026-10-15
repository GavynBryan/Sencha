# kyusu_client: a running Kyusu's operations from a shell. Connects to the
# operation socket, sends one operation or a line of stdin at a time, and prints
# each answer as one line of JSON (docs/plans/level-spatial-model.md, section 4.7).
add_executable(kyusu_client src/main.cpp)
target_link_libraries(kyusu_client PRIVATE editor_common)
sencha_warnings(kyusu_client)

set_target_properties(kyusu_client PROPERTIES
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
install(TARGETS kyusu_client RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
