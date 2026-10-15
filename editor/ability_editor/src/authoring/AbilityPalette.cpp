#include "authoring/AbilityPalette.h"

#include "authoring/AbilityExplorers.h"
#include "data/DataPath.h"
#include "data/JsonObjectEdit.h"
#include "vocabulary/VocabularyCatalog.h"

#include <authored/AuthoredClassification.h>

#include <array>
#include <format>
#include <limits>

namespace
{
constexpr std::string_view kVerb = "verb:";
constexpr std::string_view kLease = "lease:";
constexpr std::string_view kQuery = "query:";
constexpr std::string_view kKit = "kit:";

struct KitOp
{
    std::string_view Name;
    std::string_view Label;
};

constexpr std::array<KitOp, 3> kKitOps{ {
    { "set", "Set a local" },
    { "commit", "Commit costs" },
    { "cue", "Cue" },
} };

constexpr std::array<std::string_view, 4> kGroups{ "Verbs", "Leases", "Queries", "Ability" };

JsonValue Object(std::initializer_list<std::pair<std::string_view, JsonValue>> members)
{
    JsonValue object(JsonValue::Object{});
    for (const auto& [name, value] : members)
        SetMember(object, name, value);
    return object;
}

std::optional<AbilityOpList> OpListOf(const AbilityElement& op)
{
    return op.List == "ops" ? std::optional(AbilityOpList::While) : AbilityOpListNamed(op.List);
}
} // namespace

std::vector<AbilityPaletteEntry> AbilityPaletteOf(const VocabularyCatalog& names)
{
    std::vector<AbilityPaletteEntry> entries;
    for (const VocabularyCatalog::VerbRow& verb : names.ListVerbs())
    {
        const bool lease = verb.Kind == VerbKind::Lease;
        entries.push_back({ .Key = std::format("{}{}", lease ? kLease : kVerb, verb.Name),
                            .Label = verb.DisplayName.empty() ? verb.Name : verb.DisplayName,
                            .Group = lease ? "Leases" : "Verbs",
                            .Badges = std::format("{} \xC2\xB7 {}", AuthoredScopeName(verb.Scope),
                                                  AuthoredPredictionName(verb.Prediction)) });
    }
    for (const VocabularyCatalog::QueryRow& query : names.ListQueries())
        entries.push_back({ .Key = std::format("{}{}", kQuery, query.Name),
                            .Label = query.DisplayName.empty() ? query.Name : query.DisplayName,
                            .Group = "Queries" });
    for (const KitOp& op : kKitOps)
        entries.push_back({ .Key = std::format("{}{}", kKit, op.Name), .Label = std::string(op.Label), .Group = "Ability" });
    return entries;
}

std::vector<OutlineNode> AbilityPaletteNodes(const std::vector<AbilityPaletteEntry>& entries)
{
    std::vector<OutlineNode> nodes;
    for (const std::string_view group : kGroups)
    {
        nodes.push_back({ .Key = std::format("{}{}", kAbilityGroupKey, group), .Label = std::string(group) });
        for (const AbilityPaletteEntry& entry : entries)
            if (entry.Group == group)
                nodes.push_back({ .Key = entry.Key,
                                  .Label = entry.Badges.empty() ? entry.Label
                                                                : std::format("{}  ({})", entry.Label, entry.Badges),
                                  .Depth = 1 });
    }
    return nodes;
}

JsonValue AbilityPaletteOp(std::string_view entry)
{
    const JsonValue noArguments(JsonValue::Object{});
    if (entry.starts_with(kVerb))
        return Object({ { "op", JsonValue("invoke") }, { "verb", JsonValue(std::string(entry.substr(kVerb.size()))) },
                        { "arguments", noArguments } });
    if (entry.starts_with(kLease))
        return Object({ { "lease", JsonValue(std::string(entry.substr(kLease.size()))) }, { "arguments", noArguments } });
    if (entry.starts_with(kQuery))
        return Object({ { "op", JsonValue("query") }, { "query", JsonValue(std::string(entry.substr(kQuery.size()))) },
                        { "into", JsonValue("") }, { "arguments", noArguments } });
    if (entry == "kit:set")
        return Object({ { "op", JsonValue("set") }, { "local", JsonValue("") },
                        { "from", Object({ { "const", JsonValue(0) } }) } });
    if (entry == "kit:commit")
        return Object({ { "op", JsonValue("commit") }, { "groups", JsonValue(JsonValue::Array{}) } });
    if (entry == "kit:cue")
        return Object({ { "op", JsonValue("cue") }, { "cue", JsonValue("") } });
    return JsonValue();
}

std::optional<AbilityOpPlacement> AbilityOpPlacementFor(const JsonValue& data, std::string_view slot,
                                                        std::string_view selected, bool lease)
{
    constexpr std::size_t kEnd = std::numeric_limits<std::size_t>::max();
    std::optional<AbilityOpPlacement> placed;
    if (constexpr std::string_view prefix = "slot:"; slot.starts_with(prefix))
    {
        const std::string_view rest = slot.substr(prefix.size());
        const std::size_t colon = rest.rfind(':');
        if (const std::optional<AbilityOpList> list =
                colon == std::string_view::npos ? std::nullopt : AbilityOpListNamed(rest.substr(colon + 1)))
            placed = AbilityOpPlacement{ std::string(rest.substr(0, colon)), *list, kEnd };
    }
    const std::optional<AbilityElement> element =
        placed || selected.empty() ? std::nullopt : FindAbilityElement(data, selected);
    if (element && element->Kind == AbilityElementKind::Op)
        placed = AbilityOpPlacement{ element->Phase, OpListOf(*element).value_or(AbilityOpList::Enter),
                                     element->Index + 1 };
    else if (element && !element->Phase.empty())
        placed = AbilityOpPlacement{ element->Phase, AbilityOpList::Enter, kEnd };
    else if (element && element->Kind == AbilityElementKind::Phase)
        placed = AbilityOpPlacement{ element->Key, AbilityOpList::Enter, kEnd };
    if (!placed)
        return std::nullopt;
    const bool held = placed->List == AbilityOpList::Hold;
    if (lease != held)
        placed = AbilityOpPlacement{ placed->Phase, lease ? AbilityOpList::Hold : AbilityOpList::Enter, kEnd };
    return placed;
}
