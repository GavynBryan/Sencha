#include "ui/AnimationPreviewStatus.h"

#include "authoring/AnimationClipEventsSet.h"
#include "data/DataResidentSync.h"

std::string AnimationPreviewStatusText(const DataResidentState* state)
{
    if (state == nullptr)
        return {};
    switch (state->Status)
    {
    case DataResidentStatus::Current:
        return "The preview runs the working version.";
    case DataResidentStatus::Pending:
        return "Not loaded by the open rig; nothing in the preview uses it yet.";
    case DataResidentStatus::KeptLastValid:
        return "The working version was refused (" + state->Error + "); the preview keeps the last valid version.";
    }
    return {};
}

std::string AnimationPreviewStatusText(const ClipEventsPreviewState* state)
{
    if (state == nullptr)
        return {};
    switch (state->Status)
    {
    case ClipEventsPreviewStatus::Current:
        return {};
    case ClipEventsPreviewStatus::ClipNotLoaded:
        return "The clip is not loaded, so the preview cannot play these events.";
    case ClipEventsPreviewStatus::KeptLastValid:
        return "The preview keeps the last valid events: " + state->Problem;
    case ClipEventsPreviewStatus::Refused:
        return "The preview's copy of the clip could not be replaced.";
    }
    return {};
}
