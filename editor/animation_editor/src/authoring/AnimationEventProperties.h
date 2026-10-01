#pragma once

#include "properties/PropertySource.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

class AnimationClipEventsDocument;
class AnimationClipEventsSet;
class AnimationPreviewWorkspace;
struct AnimationClipEvent;

//=============================================================================
// AnimationEventProperties
//
// The clip event picked on the deck: its name, time along the clip (dragged,
// a live edit), whether it is cosmetic or gameplay and a cosmetic one's own
// weight threshold, the rig binding it raises and that binding's contract,
// the value for each of the binding's inputs, checked as the runtime would,
// and a new binding to raise instead. With no event picked, the clip's events
// as a whole: what the sidecar is and what is wrong in it.
//=============================================================================
class AnimationEventProperties final : public IPropertySource
{
public:
    AnimationEventProperties(AnimationPreviewWorkspace& workspace, std::function<std::optional<std::uint32_t>()> picked)
        : Workspace(workspace)
        , Picked(std::move(picked))
    {
    }

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    bool BeginEdit(std::string_view key) override;
    PropertyEditResult PreviewEdit(std::string_view key, std::string_view value) override;
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;
    void CancelEdit(std::string_view key) override;
    PropertyEditResult Act(std::string_view key, std::string_view verb, std::string_view choice) override;

private:
    [[nodiscard]] AnimationClipEventsDocument* Document() const;
    [[nodiscard]] const AnimationClipEvent* Event() const;
    [[nodiscard]] std::vector<PropertyNode> BindingNodes(const AnimationClipEvent& event) const;
    // `edit` applied to a copy of the picked event, kept as one step.
    PropertyEditResult Replace(const std::function<bool(AnimationClipEvent&)>& edit);

    AnimationPreviewWorkspace& Workspace;
    std::function<std::optional<std::uint32_t>()> Picked;
    // A new binding, drafted until it is created.
    std::string NewFile;
    std::string NewVerb;
    std::string NewKey;
};

// Event `key` shown at `time`, 0 to 1 along the clip, as a live edit: begun if
// none is open, reaching the preview's clip only once committed.
void PreviewEventTime(AnimationClipEventsSet& events, AnimationClipEventsDocument& document, std::uint32_t key,
                      double time);

// What an event is called where it is shown: its name, or its key when unnamed.
[[nodiscard]] std::string EventLabel(const AnimationClipEvent& event);
