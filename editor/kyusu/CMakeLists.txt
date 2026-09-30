# Kyusu, the Sencha editor: the application entry point over the workspace
# libraries.
add_executable(kyusu
    src/main.cpp
    src/app/KyusuApp.cpp
    src/app/ProjectSession.cpp
    src/app/WorkspaceKinds.cpp
)

target_link_libraries(kyusu PRIVATE level_editor material_editor data_editor animation_editor project_browser)
sencha_warnings(kyusu)
target_include_directories(kyusu PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_compile_definitions(kyusu PRIVATE
    # The product's own branding: the caption mark and the window and
    # executable icons.
    SENCHA_EDITOR_BRAND_DIR="${CMAKE_CURRENT_SOURCE_DIR}/branding"
)

# The executable's Windows icon. The .rc is generated so the path CMake knows
# is substituted outright: a resource compiler resolving it relatively differs
# between windres and rc, and there is nothing to gain by finding out how.
if(WIN32)
    enable_language(RC)
    set(SENCHA_KYUSU_ICON "${CMAKE_CURRENT_SOURCE_DIR}/branding/kyusu.ico")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/kyusu.rc.in" "${CMAKE_CURRENT_BINARY_DIR}/kyusu.rc" @ONLY)
    target_sources(kyusu PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/kyusu.rc")
endif()

# Install into the SDK (engine-roadmap.md: SDK boundary, shipped). RPATH matches the
# app's bin/ + lib/ layout.
set_target_properties(kyusu PROPERTIES
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
install(TARGETS kyusu RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
