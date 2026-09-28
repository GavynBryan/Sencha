#pragma once

class AnimationRigScenario;
class AnimationTakeComparison;
class AnimationViewportExtraction;
class EditorUiFeature;

// Dragging the blendspace point sets preview facts; nothing here edits content.
void AddAnimationBlendPanels(EditorUiFeature& ui, AnimationRigScenario& rig, AnimationTakeComparison& takes,
                             AnimationViewportExtraction& viewport);
