#pragma once

class AnimationPreviewWorkspace;
class EditorUiFeature;
class Engine;

// The host must clear Engine::OnExitRequested before destroying the workspace.
void ConfigureAnimationDocumentActions(EditorUiFeature& ui, Engine& engine,
                                       AnimationPreviewWorkspace& workspace);
