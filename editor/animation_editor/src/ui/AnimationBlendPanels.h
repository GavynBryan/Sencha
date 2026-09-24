#pragma once

class AnimationPreviewWorkspace;
class EditorUiFeature;

// The blend surfaces: each layer's blend in progress, recent blends and the
// pairwise overrides with the A/B recorder that replays an edit against a
// recorded take, and the blendspace the selected layer plays with a point to
// drag. The drag sets preview facts; nothing here edits content.
void AddAnimationBlendPanels(EditorUiFeature& ui, AnimationPreviewWorkspace& workspace);
