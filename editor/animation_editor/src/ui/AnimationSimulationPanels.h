#pragma once

class AnimationPreviewWorkspace;
class EditorUiFeature;

// The rig-under-scenario surfaces: rig and scenario setup with the dependency
// outline, fact controls, the request console, the simulation transport, and
// problems, decision history and changes. They read the workspace's
// AnimationPreviewSession and issue its live edits; none of them advances the
// simulation or compiles an asset.
void AddAnimationSimulationPanels(EditorUiFeature& ui, AnimationPreviewWorkspace& workspace);
