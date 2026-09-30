#pragma once

class AnimationPreviewWorkspace;
class AnimationPreviewRenderFeature;
struct WorkspaceView;

void AddAnimationPreviewPanels(WorkspaceView& ui, AnimationPreviewWorkspace& workspace,
                               AnimationPreviewRenderFeature*& viewport);
