#pragma once

class AnimationRigScenario;
class AnimationTakeComparison;
class AnimationViewportExtraction;
struct WorkspaceView;

// Dragging the blendspace point sets preview facts; nothing here edits content.
void AddAnimationBlendPanels(WorkspaceView& ui, AnimationRigScenario& rig, AnimationTakeComparison& takes,
                             AnimationViewportExtraction& viewport);
