#pragma once

class AnimationPreviewWorkspace;
class EditorUiFeature;

// Root motion: the curves the base layer's clip carries, whether the scenario
// moves the character and against which walls, and where it was carried
// against where it got.
void AddAnimationRootMotionPanels(EditorUiFeature& ui, AnimationPreviewWorkspace& workspace);
