# kyusu_batch: the editor's operations without a window. Opens a level, runs a
# script of operations from a file or stdin, and answers each with one line of
# JSON (docs/plans/level-spatial-model.md, section 4.7).
add_executable(kyusu_batch src/main.cpp)
target_link_libraries(kyusu_batch PRIVATE level_authoring)
sencha_warnings(kyusu_batch)

set_target_properties(kyusu_batch PROPERTIES
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
install(TARGETS kyusu_batch RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
