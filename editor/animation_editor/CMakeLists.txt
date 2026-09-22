add_library(animation_authoring STATIC src/authoring/AnimationClipPreviewSession.cpp)
target_link_libraries(animation_authoring PUBLIC sencha::engine)
target_include_directories(animation_authoring PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
sencha_warnings(animation_authoring)

add_executable(animation_editor
    src/main.cpp
    src/AnimationEditorApp.cpp
    src/AnimationPreviewWorkspace.cpp
    src/render/AnimationPreviewRenderFeature.cpp
    src/ui/AnimationPreviewPanels.cpp
    src/ui/AnimationDocumentActions.cpp
    src/ui/AnimationRequestSchemaPanel.cpp)
target_link_libraries(animation_editor PRIVATE animation_authoring editor_common)
sencha_warnings(animation_editor)
set_target_properties(animation_editor PROPERTIES
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
install(TARGETS animation_editor RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
