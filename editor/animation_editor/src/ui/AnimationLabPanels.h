#pragma once

class AnimationPreviewWorkspace;
class EditorUiFeature;

// The session laboratory: the working scenario on an authority and a client
// over a delayed, lossy link, with guesses the client makes injected. Which
// machine is which, and that the facts are synthetic, is always on screen.
void AddAnimationLabPanels(EditorUiFeature& ui, AnimationPreviewWorkspace& workspace);
