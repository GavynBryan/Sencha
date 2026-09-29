#include "authoring/AnimationSelectorEdits.h"

#include <algorithm>
#include <format>

namespace
{
    JsonValue* Rule(JsonValue& root, std::size_t rule)
    {
        JsonValue::Array* rules = AnimSelectorRules(root);
        return rules != nullptr && rule < rules->size() ? &(*rules)[rule] : nullptr;
    }

    JsonValue* Set(JsonValue& object, std::string_view key, JsonValue value)
    {
        if (!object.IsObject())
            return nullptr;
        if (JsonValue* existing = object.Find(key))
        {
            *existing = std::move(value);
            return existing;
        }
        object.AsObject().emplace_back(std::string(key), std::move(value));
        return &object.AsObject().back().second;
    }

    void Erase(JsonValue& object, std::string_view key)
    {
        if (!object.IsObject())
            return;
        std::erase_if(object.AsObject(), [&](const auto& member) { return member.first == key; });
    }

}

JsonValue::Array* AnimSelectorPredicate(JsonValue& root, std::size_t rule, std::string_view field)
{
    JsonValue* entry = Rule(root, rule);
    if (entry == nullptr || (field != "enter" && field != "stay"))
        return nullptr;
    JsonValue* rows = entry->Find(field);
    if (rows == nullptr)
        rows = Set(*entry, field, JsonValue(JsonValue::Array{}));
    return rows != nullptr && rows->IsArray() ? &rows->AsArray() : nullptr;
}

JsonValue::Array* AnimSelectorRules(JsonValue& root)
{
    JsonValue* data = root.Find("data");
    JsonValue* rules = data != nullptr ? data->Find("rules") : nullptr;
    return rules != nullptr && rules->IsArray() ? &rules->AsArray() : nullptr;
}

void AddAnimSelectorRule(JsonValue& root, std::string name, std::string behavior, int priority)
{
    JsonValue::Array* rules = AnimSelectorRules(root);
    if (rules == nullptr)
        return;
    // A rule's name is its identity across reloads, so a taken one gets a number.
    const auto taken = [&](const std::string& candidate) {
        return std::ranges::any_of(*rules, [&](const JsonValue& rule) {
            const JsonValue* existing = rule.IsObject() ? rule.Find("name") : nullptr;
            return existing != nullptr && existing->IsString() && existing->AsString() == candidate;
        });
    };
    const std::string base = name;
    for (int n = 2; taken(name); ++n)
        name = std::format("{} {}", base, n);
    rules->emplace_back(JsonValue::Object{
        { "name", JsonValue(std::move(name)) },
        { "priority", JsonValue(priority) },
        { "enter", JsonValue(JsonValue::Array{}) },
        { "behavior", JsonValue(std::move(behavior)) },
    });
}

bool RemoveAnimSelectorRule(JsonValue& root, std::size_t rule)
{
    JsonValue::Array* rules = AnimSelectorRules(root);
    if (rules == nullptr || rule >= rules->size())
        return false;
    rules->erase(rules->begin() + static_cast<std::ptrdiff_t>(rule));
    return true;
}

bool MoveAnimSelectorRule(JsonValue& root, std::size_t from, std::size_t to)
{
    JsonValue::Array* rules = AnimSelectorRules(root);
    if (rules == nullptr || from >= rules->size() || to >= rules->size() || from == to)
        return false;
    JsonValue moved = std::move((*rules)[from]);
    rules->erase(rules->begin() + static_cast<std::ptrdiff_t>(from));
    rules->insert(rules->begin() + static_cast<std::ptrdiff_t>(to), std::move(moved));
    return true;
}

bool SetAnimSelectorStay(JsonValue& root, std::size_t rule, bool present)
{
    JsonValue* entry = Rule(root, rule);
    if (entry == nullptr)
        return false;
    if (!present)
    {
        Erase(*entry, "stay");
        return true;
    }
    if (entry->Find("stay") != nullptr)
        return false;
    const JsonValue* enter = entry->Find("enter");
    Set(*entry, "stay", enter != nullptr ? *enter : JsonValue(JsonValue::Array{}));
    return true;
}
