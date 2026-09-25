#pragma once

class AnimationPreviewWorkspace;
class EditorUiFeature;

// The migration report: scenes that still name the retired clip player, and
// turning each into a one-layer rig that plays the clip as the player did.
void AddAnimationMigrationPanels(EditorUiFeature& ui, AnimationPreviewWorkspace& workspace);
