#pragma once

#include "operations/EditorOperationCatalog.h"
#include "operations/LevelTarget.h"

#include <core/identity/Id.h>
#include <core/json/JsonValue.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// A draft as a file: its steps, the level they apply to, the digest each
// entity it changes or relies on had then, and the digest of each operation's
// contract, so a replay onto a level or an editor that moved on says so. What
// it changes is never stored; replaying the steps computes it.
struct LevelProposal
{
    std::string Label;
    std::string Rationale;
    std::string Author;
    std::uint32_t IdSeed = 0;
    std::string BasePath;
    std::vector<std::pair<PersistentEntityId, std::uint64_t>> BaseDigests;
    // By operation name; absent from files written before contracts were recorded.
    std::vector<std::pair<std::string, std::uint64_t>> Contracts;
    std::vector<LevelDraftStep> Steps;
};

[[nodiscard]] std::uint64_t ContractDigest(const EditorOperationCatalog& catalog, std::string_view operation);
[[nodiscard]] LevelProposal ProposalOf(const LevelDraft& draft, std::string author, const EditorOperationCatalog& catalog);
[[nodiscard]] JsonValue LevelProposalToJson(const LevelProposal& proposal);
// Nullopt, with `error` set, for anything but a version 1 proposal.
[[nodiscard]] std::optional<LevelProposal> LevelProposalFromJson(const JsonValue& json, std::string& error);
