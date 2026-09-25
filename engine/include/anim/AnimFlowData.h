#pragma once

#include <anim/AnimBehaviorSet.h>
#include <anim/AnimPredicate.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <core/metadata/DataSchema.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

inline constexpr std::string_view kAnimFlowType = "animation.flow";
inline constexpr std::size_t kAnimFlowMaxSections = 32;

enum class AnimFlowLoop : std::uint8_t
{
    Once,
    // Replays the section while a predicate holds at its end.
    While,
    // As many times as a parameter of the flow's request says.
    Count,
};

enum class AnimCancelTiming : std::uint8_t
{
    AtSectionEnd,
    Immediate,
};

[[nodiscard]] std::string_view AnimFlowLoopName(AnimFlowLoop loop);

struct AnimFlowBranchDecl
{
    // A later section, by tag.
    std::string To;
    AnimPredicateDecl When;
};

struct AnimFlowSectionDecl
{
    std::string Tag;
    // Exactly one: a clip, or a behavior resolved through the slot map on entry.
    std::string Clip;
    std::string Slot;
    AnimFlowLoop Loop = AnimFlowLoop::Once;
    AnimPredicateDecl While;
    // Count loops: the request intent and the parameter holding the count.
    std::string CountIntent;
    std::string CountParam;
    // Tried in order at the section's end, after the loop.
    std::vector<AnimFlowBranchDecl> Branches;
    // With no branch taken, the flow ends here instead of moving on.
    bool Ends = false;
    AnimCancelTiming CancelTiming = AnimCancelTiming::AtSectionEnd;
};

struct AnimFlowData
{
    std::vector<AnimFlowSectionDecl> Sections;
    // The cancel section's tag; empty when cancelling simply ends the flow.
    std::string Cancel;
    // Handed the section's tag as the `section` input.
    std::optional<AnimLifecycleDecl> SectionEntered;
    std::optional<AnimLifecycleDecl> SectionExited;

    [[nodiscard]] int FindSection(std::string_view tag) const;
};

inline constexpr std::string_view kAnimSectionLifecycleInput = "section";

void RegisterAnimFlowData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas);
