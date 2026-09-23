#pragma once

class AnimationPreviewWorkspace;
class EditorUiFeature;

// The event surfaces: a clip's event track with its inspector, and the
// admissions the preview's crossings received. They edit through the
// workspace's events documents and read the simulation's history; neither
// advances the simulation, and nothing they show is proof a game ran.
void AddAnimationEventPanels(EditorUiFeature& ui, AnimationPreviewWorkspace& workspace);
