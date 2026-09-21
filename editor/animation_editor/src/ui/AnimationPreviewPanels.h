#pragma once

class AnimationPreviewWorkspace;
class AnimationPreviewRenderFeature;
class EditorUiFeature;

void AddAnimationPreviewPanels(EditorUiFeature& ui, AnimationPreviewWorkspace& workspace,
                               AnimationPreviewRenderFeature*& viewport);
