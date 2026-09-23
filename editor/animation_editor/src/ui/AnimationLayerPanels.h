#pragma once

class AnimationPreviewWorkspace;
class EditorUiFeature;

// The layer surfaces: the layer stack with its weights and preview-only mute
// and solo, the rig's skeleton with each layer's mask coverage and the mask
// steps a right-click adds, and the flow the selected layer plays as a section
// strip with its cancel lane. Mask edits go through the rig document, one undo
// step each; nothing else here edits content.
void AddAnimationLayerPanels(EditorUiFeature& ui, AnimationPreviewWorkspace& workspace);
