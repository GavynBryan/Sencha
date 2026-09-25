#pragma once

#include <string>

class AnimationPreviewWorkspace;
class EditorUiFeature;

// Only mask edits change content, each one undo step on the rig document.
void AddAnimationLayerPanels(EditorUiFeature& ui, AnimationPreviewWorkspace& workspace);

// Menu items for the selected layer's mask; shared by the skeleton tree and viewport.
void DrawAnimationMaskMenu(AnimationPreviewWorkspace& workspace, const std::string& joint);
