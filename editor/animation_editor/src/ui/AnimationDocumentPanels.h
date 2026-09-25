#pragma once

class AnimationPreviewWorkspace;
class EditorUiFeature;

// The active animation document as its schema's form: every field of every
// animation asset, including those no purpose-built panel edits.
void AddAnimationDocumentPanels(EditorUiFeature& ui, AnimationPreviewWorkspace& workspace);
