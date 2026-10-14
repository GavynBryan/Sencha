#pragma once

#include "operations/LevelTarget.h"

#include <core/identity/Id.h>
#include <core/json/JsonValue.h>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// A draft as a file: its steps, the level they apply to, and the digest each
// entity it changes had then, so a replay onto a level that moved on says so.
// What it changes is never stored; replaying the steps computes it.
struct LevelProposal
{
    std::string Label;
    std::string Rationale;
    std::string Author;
    std::uint32_t IdSeed = 0;
    std::string BasePath;
    std::vector<std::pair<PersistentEntityId, std::uint64_t>> BaseDigests;
    std::vector<LevelDraftStep> Steps;
};

[[nodiscard]] LevelProposal ProposalOf(const LevelDraft& draft, std::string author);
[[nodiscard]] JsonValue LevelProposalToJson(const LevelProposal& proposal);
// Nullopt, with `error` set, for anything but a version 1 proposal.
[[nodiscard]] std::optional<LevelProposal> LevelProposalFromJson(const JsonValue& json, std::string& error);
