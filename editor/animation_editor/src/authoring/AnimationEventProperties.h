#pragma once

#include "properties/PropertySource.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

class AnimationClipEventsDocument;
class AnimationPreviewWorkspace;
struct AnimationClipEvent;

// The clip event picked on the deck: its name, its time along the clip (a live
// edit), its scope and a cosmetic one's weight threshold, the rig binding it
// raises with that binding's inputs checked as the runtime would, and a new
// binding to raise instead. With none picked, what is wrong in the clip's events.
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

// What an event is called where it is shown: its name, or its key when unnamed.
[[nodiscard]] std::string EventLabel(const AnimationClipEvent& event);
