#pragma once

#include "properties/PropertySource.h"

#include <anim/AnimSelectSystem.h>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

class AnimationPreviewWorkspace;

// The Rules lens's card: a layer's rules and how each fared, or one picked rule
// with why it won or lost and what it selects; details are every layer's decision.
class AnimationRuleProperties final : public CommitOnlyPropertySource
{
public:
    // `open` brings a document up on the card: a selector, behavior set or slot map.
    AnimationRuleProperties(AnimationPreviewWorkspace& workspace, std::function<void(const std::string&)> open)
        : Workspace(workspace)
        , Open(std::move(open))
    {
    }

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;
    PropertyEditResult Act(std::string_view key, std::string_view verb, std::string_view choice) override;

    // The decisions recorded on the inspected tick, every layer's.
    [[nodiscard]] IPropertySource& Decisions() { return TickDecisions; }
    // The card's title: the picked rule, or the layer whose rules it lists.
    [[nodiscard]] std::string Title() const;

private:
    class TickSource final : public ReadOnlyPropertySource
    {
    public:
        explicit TickSource(const AnimationRuleProperties& owner) : Owner(owner) {}
        [[nodiscard]] std::vector<PropertyNode> Nodes() const override;

    private:
        const AnimationRuleProperties& Owner;
    };

    // How the layer's rules fared on the inspected tick, or the next one.
    [[nodiscard]] std::vector<AnimRuleVerdict> Verdicts(std::size_t layer) const;
    [[nodiscard]] std::vector<PropertyNode> RuleNodes() const;
    [[nodiscard]] std::vector<PropertyNode> BehaviorNodes() const;

    AnimationPreviewWorkspace& Workspace;
    std::function<void(const std::string&)> Open;
    TickSource TickDecisions{ *this };
    bool ShowNext = false;
};
