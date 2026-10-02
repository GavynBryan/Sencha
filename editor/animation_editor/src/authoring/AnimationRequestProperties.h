#pragma once

#include "authoring/AnimationScenario.h"

#include "properties/PropertySource.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class AnimationPreviewWorkspace;

//=============================================================================
// AnimationRequestProperties
//
// The Requests lens's card: a request to issue on the next tick -- from which
// participant, which intent, for how long, on which layers, with the intent's
// parameters -- the live records with a Cancel for a reason, and how the last
// tick's requests fared. Issuing and cancelling are recorded scenario edits.
//=============================================================================
class AnimationRequestProperties final : public IPropertySource
{
public:
    explicit AnimationRequestProperties(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    bool BeginEdit(std::string_view key) override { return key.starts_with("issue."); }
    PropertyEditResult PreviewEdit(std::string_view, std::string_view) override { return PropertyEditResult::Unchanged; }
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;
    void CancelEdit(std::string_view) override {}
    PropertyEditResult Act(std::string_view key, std::string_view verb, std::string_view choice) override;

private:
    [[nodiscard]] PropertyNode IssueNodes() const;
    [[nodiscard]] PropertyNode LiveNodes() const;

    AnimationPreviewWorkspace& Workspace;
    // The request being drafted.
    std::size_t Participant = 0;
    std::size_t Intent = 0;
    std::string FreeIntent;
    AnimRequestLifetime Lifetime = AnimRequestLifetime::Held;
    std::uint32_t FixedTicks = 30;
    std::uint8_t Layers = kAnimAllLayers;
    std::vector<AnimationScenarioValue> Params;
};
