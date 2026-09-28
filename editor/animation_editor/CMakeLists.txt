add_library(animation_authoring STATIC
    src/authoring/AnimationClipEventsDocument.cpp
    src/authoring/AnimationClipEventsSet.cpp
    src/authoring/AnimationClipPlayerMigration.cpp
    src/authoring/AnimationTraceImport.cpp
    src/authoring/AnimationBlendComparison.cpp
    src/authoring/AnimationClipPreviewSession.cpp
    src/authoring/AnimationEventBindings.cpp
    src/authoring/AnimationJointPicking.cpp
    src/authoring/AnimationNameDeclarations.cpp
    src/authoring/AnimationFlowEdits.cpp
    src/authoring/AnimationPredicateEdits.cpp
    src/authoring/AnimationPredicateText.cpp
    src/authoring/AnimationPreviewSession.cpp
    src/authoring/AnimationPreviewWorkspace.cpp
    src/authoring/AnimationRigEdits.cpp
    src/authoring/AnimationRigRecipe.cpp
    src/authoring/AnimationRigOutline.cpp
    src/authoring/AnimationScenario.cpp
    src/authoring/AnimationScenarioBatch.cpp
    src/authoring/AnimationSelectorEdits.cpp
    src/authoring/AnimationSessionLab.cpp)
target_link_libraries(animation_authoring PUBLIC editor_common)
target_include_directories(animation_authoring PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
sencha_warnings(animation_authoring)

add_executable(animation_editor
    src/main.cpp
    src/AnimationEditorApp.cpp
    src/render/AnimationPreviewRenderFeature.cpp
    src/ui/AnimationPreviewPanels.cpp
    src/ui/AnimationBlendPanels.cpp
    src/ui/AnimationDocumentActions.cpp
    src/ui/AnimationDocumentWidgets.cpp
    src/ui/AnimationEventPanels.cpp
    src/ui/AnimationLabPanels.cpp
    src/ui/AnimationLayerPanels.cpp
    src/ui/AnimationDocumentPanels.cpp
    src/ui/AnimationMigrationPanels.cpp
    src/ui/AnimationScenarioBatchPanels.cpp
    src/ui/AnimationRequestSchemaPanel.cpp
    src/ui/AnimationRootMotionPanels.cpp
    src/ui/AnimationSelectionPanels.cpp
    src/ui/AnimationSimulationPanels.cpp)
target_link_libraries(animation_editor PRIVATE animation_authoring editor_common)
sencha_warnings(animation_editor)
set_target_properties(animation_editor PROPERTIES
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
install(TARGETS animation_editor RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
