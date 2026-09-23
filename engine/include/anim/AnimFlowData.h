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

//=============================================================================
// Flow (`animation.flow`)
//
// A forward-only sequence of sections with one cancel section: the content a
// reload, a combo swing or a ledge climb plays. A section plays a clip, or a
// slot resolved through the rig's slot map when the section is entered, and
// either plays once, loops while a predicate holds, or loops a count a request
// parameter gives. The cancel section always ends the flow when it finishes.
//
// Control only goes forward. At a section's end the flow loops it, takes the
// first forward branch whose predicate passes, or moves to the next section;
// it may also go to the cancel section, at once or at the section's end. A
// backward branch fails to compile: a sequence that needs to go back is making
// a decision, and decisions belong to gameplay, which answers a section's
// lifecycle event with a new request.
//=============================================================================

inline constexpr std::string_view kAnimFlowType = "animation.flow";
inline constexpr std::size_t kAnimFlowMaxSections = 32;

enum class AnimFlowLoop : std::uint8_t
{
    Once,
    // Replays the section while a predicate over facts and requests holds at
    // its end.
    While,
    // Plays the section as many times as a parameter of the flow's request says.
    Count,
};

enum class AnimCancelTiming : std::uint8_t
{
    // Finish this section, then go to the cancel section.
    AtSectionEnd,
    // Go to the cancel section now.
    Immediate,
};

[[nodiscard]] std::string_view AnimFlowLoopName(AnimFlowLoop loop);

struct AnimFlowBranchDecl
{
    // The later section this branch jumps to, by tag.
    std::string To;
    AnimPredicateDecl When;
};

struct AnimFlowSectionDecl
{
    // The section's tag: its identity, and what its lifecycle events carry.
    std::string Tag;
    // Exactly one of these: a clip, or a behavior resolved through the slot
    // map when the section is entered and pinned for the section.
    std::string Clip;
    std::string Slot;
    AnimFlowLoop Loop = AnimFlowLoop::Once;
    AnimPredicateDecl While;
    // Count loops: the request intent and the parameter holding the count.
    std::string CountIntent;
    std::string CountParam;
    // Tried in order at the section's end, after the loop.
    std::vector<AnimFlowBranchDecl> Branches;
    // When no branch is taken the flow ends here rather than moving to the
    // next section: the ledge climb's last move, before a drop that only a
    // cancel reaches.
    bool Ends = false;
    AnimCancelTiming CancelTiming = AnimCancelTiming::AtSectionEnd;
};

struct AnimFlowData
{
    std::vector<AnimFlowSectionDecl> Sections;
    // The cancel section's tag; empty when cancelling simply ends the flow.
    std::string Cancel;
    // Invoked as each section is entered and left, handed the section's tag as
    // its `section` input.
    std::optional<AnimLifecycleDecl> SectionEntered;
    std::optional<AnimLifecycleDecl> SectionExited;

    [[nodiscard]] int FindSection(std::string_view tag) const;
};

// The one input a section lifecycle event supplies.
inline constexpr std::string_view kAnimSectionLifecycleInput = "section";

void RegisterAnimFlowData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas);
