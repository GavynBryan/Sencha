#pragma once

#include <string>

class AnimationPreviewWorkspace;
class EditorUiFeature;

// The layer surfaces: the layer stack with its weights and preview-only mute
// and solo, the rig's skeleton with each layer's mask coverage and the mask
// steps a right-click adds, and the flow the selected layer plays as a section
// strip with its cancel lane. Mask edits go through the rig document, one undo
// step each; nothing else here edits content.
void AddAnimationLayerPanels(EditorUiFeature& ui, AnimationPreviewWorkspace& workspace);

// The mask steps for `joint` on the selected layer, as menu items: what the
// skeleton tree and the viewport offer on right-click. Each is one undo step
// on the rig document.
void DrawAnimationMaskMenu(AnimationPreviewWorkspace& workspace, const std::string& joint);
