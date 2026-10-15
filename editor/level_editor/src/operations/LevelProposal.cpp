#include "LevelProposal.h"

#include "document/EntityChangeSet.h"
#include "operations/OperationReference.h"

#include <core/hash/ContentHash.h>

#include <algorithm>
#include <charconv>
#include <format>
#include <span>

namespace
{
    constexpr std::string_view kType = "level.proposal";
    constexpr int kVersion = 1;

    std::string TextOr(const JsonValue& json, std::string_view key)
    {
        const JsonValue* value = json.Find(key);
        return value != nullptr && value->IsString() ? value->AsString() : std::string{};
    }

    std::optional<std::uint64_t> DigestFromText(std::string_view text)
    {
        std::uint64_t value = 0;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value, 16);
        if (text.size() != 16 || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
            return std::nullopt;
        return value;
    }
}

std::uint64_t ContractDigest(const EditorOperationCatalog& catalog, std::string_view operation)
{
    const EditorOperationDefinition* definition = catalog.Get(catalog.Find(operation));
    if (definition == nullptr)
        return 0;
    const std::string contract = OperationContract(*definition);
    return HashBytes64(std::as_bytes(std::span(contract)));
}

LevelProposal ProposalOf(const LevelDraft& draft, std::string author, const EditorOperationCatalog& catalog)
{
    LevelProposal proposal;
    proposal.Label = draft.Label;
    proposal.Rationale = draft.Rationale;
    proposal.Author = std::move(author);
    proposal.IdSeed = draft.IdSeed;
    proposal.BasePath = draft.BasePath;
    for (const EntityChange& change : draft.Changes.Changes)
        if (change.Before)
            proposal.BaseDigests.emplace_back(change.Id, DigestOf(*change.Before));
    for (const EntityRead& read : draft.Changes.Reads)
        proposal.BaseDigests.emplace_back(read.Id, read.Digest);
    proposal.Steps = draft.Steps;
    for (const LevelDraftStep& step : draft.Steps)
        if (std::ranges::find(proposal.Contracts, step.Operation, &std::pair<std::string, std::uint64_t>::first)
            == proposal.Contracts.end())
            proposal.Contracts.emplace_back(step.Operation, ContractDigest(catalog, step.Operation));
    return proposal;
}

JsonValue LevelProposalToJson(const LevelProposal& proposal)
{
    JsonValue::Object digests;
    for (const auto& [id, digest] : proposal.BaseDigests)
        digests.emplace_back(PersistentEntityIdToString(id), JsonValue(std::format("{:016x}", digest)));
    JsonValue::Object contracts;
    for (const auto& [operation, digest] : proposal.Contracts)
        contracts.emplace_back(operation, JsonValue(std::format("{:016x}", digest)));
    JsonValue::Array steps;
    for (const LevelDraftStep& step : proposal.Steps)
        steps.push_back(JsonValue(JsonValue::Object{
            { "op", JsonValue(step.Operation) },
            { "params", step.Arguments },
        }));
    return JsonValue(JsonValue::Object{
        { "type", JsonValue(std::string(kType)) },
        { "version", JsonValue(static_cast<double>(kVersion)) },
        { "label", JsonValue(proposal.Label) },
        { "rationale", JsonValue(proposal.Rationale) },
        { "author", JsonValue(proposal.Author) },
        { "id_seed", JsonValue(static_cast<double>(proposal.IdSeed)) },
        { "base", JsonValue(JsonValue::Object{
            { "document", JsonValue(proposal.BasePath) },
            { "digests", JsonValue(std::move(digests)) },
        }) },
        { "contracts", JsonValue(std::move(contracts)) },
        { "operations", JsonValue(std::move(steps)) },
    });
}

std::optional<LevelProposal> LevelProposalFromJson(const JsonValue& json, std::string& error)
{
    const auto fail = [&error](std::string why) -> std::optional<LevelProposal> {
        error = std::move(why);
        return std::nullopt;
    };
    if (!json.IsObject() || TextOr(json, "type") != kType)
        return fail("not a level proposal");
    const JsonValue* version = json.Find("version");
    if (version == nullptr || !version->IsNumber() || version->AsNumber() != kVersion)
        return fail("not a version " + std::to_string(kVersion) + " level proposal");

    LevelProposal proposal;
    proposal.Label = TextOr(json, "label");
    proposal.Rationale = TextOr(json, "rationale");
    proposal.Author = TextOr(json, "author");
    const JsonValue* seed = json.Find("id_seed");
    if (seed == nullptr || !seed->IsNumber())
        return fail("id_seed is missing");
    proposal.IdSeed = static_cast<std::uint32_t>(seed->AsNumber());

    const JsonValue* base = json.Find("base");
    if (base == nullptr || !base->IsObject())
        return fail("base is missing");
    proposal.BasePath = TextOr(*base, "document");
    if (const JsonValue* digests = base->Find("digests"); digests != nullptr && digests->IsObject())
        for (const auto& [key, value] : digests->AsObject())
        {
            const std::optional<PersistentEntityId> id = PersistentEntityIdFromString(key);
            const std::optional<std::uint64_t> digest =
                value.IsString() ? DigestFromText(value.AsString()) : std::nullopt;
            if (!id || !digest)
                return fail("base digest '" + key + "' is malformed");
            proposal.BaseDigests.emplace_back(*id, *digest);
        }

    if (const JsonValue* contracts = json.Find("contracts"); contracts != nullptr && contracts->IsObject())
        for (const auto& [operation, value] : contracts->AsObject())
        {
            const std::optional<std::uint64_t> digest = value.IsString() ? DigestFromText(value.AsString()) : std::nullopt;
            if (!digest)
                return fail("contract '" + operation + "' is malformed");
            proposal.Contracts.emplace_back(operation, *digest);
        }

    const JsonValue* operations = json.Find("operations");
    if (operations == nullptr || !operations->IsArray())
        return fail("operations is missing");
    for (const JsonValue& step : operations->AsArray())
    {
        const JsonValue* params = step.Find("params");
        if (TextOr(step, "op").empty() || params == nullptr || !params->IsObject())
            return fail("operation " + std::to_string(proposal.Steps.size() + 1) + " needs an op and params");
        proposal.Steps.push_back({ TextOr(step, "op"), *params });
    }
    return proposal;
}
