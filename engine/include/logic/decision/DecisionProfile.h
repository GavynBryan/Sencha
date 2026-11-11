#pragma once

#include <core/assets/AssetRef.h>
#include <core/json/JsonValue.h>
#include <core/metadata/DataSchema.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// How an agent carries out one of its table's options.
struct DecisionOptionPlan
{
    std::string Option{};
    // The ability the option runs, by asset path; the agent must be granted it.
    std::string Ability{};
    // A candidates.evaluation that chooses where the option goes; empty for none.
    std::string Position{};
    // Goes where the target was last known to be instead.
    bool ToLastSeen = false;
    // The chosen place is chosen again once the target has moved this far from
    // where it was then, or once the place is this old.
    float RepositionDistance = 4.0f;
    float PositionSeconds = 8.0f;
    // A place reached is spent, so the option chooses its next one.
    bool ReplaceOnArrival = false;
    // A claim the option needs a place in within the agent's group; empty for
    // none. Its capacity is ClaimCount, or ClaimShare of the group if that is more.
    std::string Claim{};
    std::uint32_t ClaimCount = 1;
    float ClaimShare = 0.0f;
    // What the option's place reaches is recorded for the group as visited.
    bool MarksVisited = false;
    // Once begun, nothing else is considered until its ability ends.
    bool Commits = false;
};

struct DecisionProfile
{
    // Remembered subjects carrying this tag, or one beneath it, are targets.
    std::string Hostile{};
    // The selection.table the options come from.
    std::string Table{};
    float DecisionSeconds = 0.25f;
    // How long an option whose place evaluation found nowhere stays ruled out.
    float UnavailableSeconds = 1.0f;
    std::vector<DecisionOptionPlan> Options{};

    [[nodiscard]] const DecisionOptionPlan* PlanFor(std::string_view option) const;
};

// Reads the members MakeDecisionProfileFields declares from data already checked
// against them. Each asset a profile names is added to `dependencies`. An empty
// string, or what is wrong.
[[nodiscard]] std::string ReadDecisionProfile(const JsonValue& data, DecisionProfile& out,
                                              std::vector<AssetRef>& dependencies);

// A profile's members, with `optionMembers` added to each option's record for
// what the game keeps per option beside the plan.
[[nodiscard]] std::vector<DataFieldSchema> MakeDecisionProfileFields(std::vector<DataFieldSchema> optionMembers);
