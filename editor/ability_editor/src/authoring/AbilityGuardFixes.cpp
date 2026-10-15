#include "authoring/AbilityGuardFixes.h"

#include "authoring/AbilityArguments.h"
#include "authoring/AbilityConditionText.h"
#include "authoring/AbilityDocumentEdits.h"
#include "authoring/AbilityElements.h"
#include "data/DataPath.h"
#include "data/JsonObjectEdit.h"
#include "identity/RandomKey.h"

#include <algorithm>
#include <format>

namespace
{
// The value at the end of the problem's path, read where a guard can check it.
const JsonValue* GuardedValue(const JsonValue& data, std::string_view path)
{
    const JsonValue* value = FindDataValue(data, path);
    return value != nullptr && (FindMember(*value, "local") != nullptr || FindMember(*value, "port") != nullptr)
             ? value
             : nullptr;
}

std::string Identity(const JsonValue& op, std::string_view path, const VocabularyCatalog& names)
{
    const std::size_t at = path.rfind(".arguments.");
    const DataFieldSchema* contract = AbilityOpContract(op, names);
    if (at == std::string_view::npos || contract == nullptr)
        return {};
    const std::string_view argument = path.substr(at + std::string_view(".arguments.").size());
    const auto field = std::ranges::find(contract->Children, argument, &DataFieldSchema::Key);
    if (field == contract->Children.end())
        return {};
    const DataFieldSchema& shown =
        field->Kind == DataFieldKind::Optional && field->Children.size() == 1 ? field->Children.front() : *field;
    return shown.Reference.ComponentIdentity;
}
} // namespace

std::optional<AbilityGuardFix> AbilityGuardFixFor(const JsonValue& data, const AbilityDiagnostic& diagnostic,
                                                  const VocabularyCatalog& names)
{
    const bool empty = diagnostic.Code == "unguarded_maybe_empty";
    if (!empty && diagnostic.Code != "unguarded_recipient")
        return std::nullopt;
    const std::optional<AbilityElement> op = AbilityElementAt(data, diagnostic.Path);
    const JsonValue* value = GuardedValue(data, diagnostic.Path);
    if (!op || op->Kind != AbilityElementKind::Op || value == nullptr)
        return std::nullopt;
    JsonValue condition(JsonValue::Object{});
    if (empty)
    {
        const std::string local = ReadMemberString(*value, "local");
        if (local.empty())
            return std::nullopt;
        SetMember(condition, "hit", JsonValue(local));
    }
    else
    {
        JsonValue entity(JsonValue::Object{ { "of", *value } });
        if (const std::string identity = Identity(*FindDataValue(data, op->Path), diagnostic.Path, names);
            !identity.empty())
            SetMember(entity, "has", JsonValue(JsonValue::Array{ JsonValue(identity) }));
        SetMember(condition, "entity", std::move(entity));
    }
    std::string label = std::format("Guard with '{}'", AbilityConditionText(data, condition));
    return AbilityGuardFix{ .Element = op->Key, .Condition = std::move(condition), .Label = std::move(label) };
}

bool ApplyAbilityGuardFix(JsonValue& data, const AbilityGuardFix& fix)
{
    const std::optional<AbilityElement> op = FindAbilityElement(data, fix.Element);
    JsonValue* held = op ? FindDataValue(data, op->Path) : nullptr;
    if (held == nullptr)
        return false;
    JsonValue* when = FindMember(*held, "when");
    if (when == nullptr)
        return !SetAbilityGuard(data, fix.Element, fix.Condition).empty();
    JsonValue added = fix.Condition;
    SetMember(added, "key", JsonValue(MintStableKey()));
    if (JsonValue* all = FindMember(*when, "all"); all != nullptr && all->IsArray())
    {
        all->AsArray().push_back(std::move(added));
        return true;
    }
    *when = JsonValue(JsonValue::Object{ { "key", JsonValue(MintStableKey()) },
                                         { "all", JsonValue(JsonValue::Array{ std::move(*when), std::move(added) }) } });
    return true;
}
