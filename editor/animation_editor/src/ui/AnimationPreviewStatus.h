#pragma once

#include <string>

struct ClipEventsPreviewState;
struct DataResidentState;

// Whether the preview runs a document's working version; empty when nothing was pushed.
[[nodiscard]] std::string AnimationPreviewStatusText(const DataResidentState* state);
// Empty when the preview plays the working events.
[[nodiscard]] std::string AnimationPreviewStatusText(const ClipEventsPreviewState* state);
