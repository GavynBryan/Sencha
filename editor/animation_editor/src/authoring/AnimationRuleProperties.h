#pragma once

#include "properties/PropertySource.h"

#include <anim/AnimSelectSystem.h>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

class AnimationPreviewWorkspace;

//=============================================================================
// AnimationRuleProperties
//
// The Rules lens's card. With no rule picked, a layer's rules and how each
// fared on the inspected tick, or on the next when asked while paused. With
// one picked: why it won or lost, what it reads, where it was authored, the
// behavior it selects and that behavior's policy, the slot rows that resolve
// the behavior to content, and the other rules that select it. Its details are
// what every layer decided on the inspected tick.
//=============================================================================
class AnimationRuleProperties final : public IPropertySource
{
public:
    // `open` brings a document up on the card: a selector, behavior set or slot map.
    AnimationRuleProperties(AnimationPreviewWorkspace& workspace, std::function<void(const std::string&)> open)
        : Workspace(workspace)
        , Open(std::move(open))
    {
    }

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    bool BeginEdit(std::string_view key) override { return key == "layer" || key == "next"; }
    PropertyEditResult PreviewEdit(std::string_view, std::string_view) override { return PropertyEditResult::Unchanged; }
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;
    void CancelEdit(std::string_view) override {}
    PropertyEditResult Act(std::string_view key, std::string_view verb, std::string_view choice) override;

    // The decisions recorded on the inspected tick, every layer's.
    [[nodiscard]] IPropertySource& Decisions() { return TickDecisions; }
    // The card's title: the picked rule, or the layer whose rules it lists.
    [[nodiscard]] std::string Title() const;

private:
    class TickSource final : public IPropertySource
    {
    public:
        explicit TickSource(const AnimationRuleProperties& owner) : Owner(owner) {}
        [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
        bool BeginEdit(std::string_view) override { return false; }
        PropertyEditResult PreviewEdit(std::string_view, std::string_view) override { return PropertyEditResult::Gone; }
        PropertyEditResult CommitEdit(std::string_view, std::string_view) override { return PropertyEditResult::Gone; }
        void CancelEdit(std::string_view) override {}

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
