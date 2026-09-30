#pragma once

class AnimationPreviewWorkspace;
class AnimationPreviewRenderFeature;
class PromptQueue;
struct WorkspaceView;

void AddAnimationPreviewPanels(WorkspaceView& ui, AnimationPreviewWorkspace& workspace,
                               AnimationPreviewRenderFeature*& viewport, PromptQueue& prompts);
