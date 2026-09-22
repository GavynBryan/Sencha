#include "authoring/AnimationSelectorEdits.h"

#include <algorithm>

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

    // A predicate field's rows, created empty when the rule has none.
    JsonValue::Array* Rows(JsonValue& root, std::size_t rule, std::string_view field)
    {
        JsonValue* entry = Rule(root, rule);
        if (entry == nullptr || (field != "enter" && field != "stay"))
            return nullptr;
        JsonValue* rows = entry->Find(field);
        if (rows == nullptr)
            rows = Set(*entry, field, JsonValue(JsonValue::Array{}));
        return rows != nullptr && rows->IsArray() ? &rows->AsArray() : nullptr;
    }
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

bool AddAnimPredicateRow(JsonValue& root, std::size_t rule, std::string_view field, JsonValue test)
{
    JsonValue::Array* rows = Rows(root, rule, field);
    if (rows == nullptr)
        return false;
    rows->push_back(std::move(test));
    return true;
}

bool RemoveAnimPredicateRow(JsonValue& root, std::size_t rule, std::string_view field, std::size_t row)
{
    JsonValue::Array* rows = Rows(root, rule, field);
    if (rows == nullptr || row >= rows->size())
        return false;
    rows->erase(rows->begin() + static_cast<std::ptrdiff_t>(row));
    return true;
}

bool AddAnimPredicateAlternative(JsonValue& root, std::size_t rule, std::string_view field, std::size_t row,
                                 JsonValue test)
{
    JsonValue::Array* rows = Rows(root, rule, field);
    if (rows == nullptr || row >= rows->size())
        return false;
    JsonValue& entry = (*rows)[row];
    if (JsonValue* any = entry.Find("any"); any != nullptr && any->IsArray())
    {
        if (any->AsArray().size() >= kAnimMaxAnyOf)
            return false;
        any->AsArray().push_back(std::move(test));
        return true;
    }
    entry = JsonValue(JsonValue::Object{ { "any", JsonValue(JsonValue::Array{ std::move(entry), std::move(test) }) } });
    return true;
}

bool RemoveAnimPredicateAlternative(JsonValue& root, std::size_t rule, std::string_view field, std::size_t row,
                                    std::size_t alternative)
{
    JsonValue::Array* rows = Rows(root, rule, field);
    if (rows == nullptr || row >= rows->size())
        return false;
    JsonValue* any = (*rows)[row].Find("any");
    if (any == nullptr || !any->IsArray() || alternative >= any->AsArray().size())
        return false;
    JsonValue::Array& tests = any->AsArray();
    tests.erase(tests.begin() + static_cast<std::ptrdiff_t>(alternative));
    // A group of one is just that test.
    if (tests.size() == 1)
    {
        JsonValue remaining = std::move(tests.front());
        (*rows)[row] = std::move(remaining);
    }
    return true;
}

JsonValue MakeAnimFactTest(std::string fact, AnimFactKind kind)
{
    JsonValue::Object test{ { "fact", JsonValue(std::move(fact)) } };
    switch (kind)
    {
    case AnimFactKind::Bool:
        break;
    case AnimFactKind::Float:
    case AnimFactKind::Int:
        test.emplace_back("compare", JsonValue("gt"));
        test.emplace_back("value", JsonValue(0.0));
        break;
    case AnimFactKind::Tag:
        test.emplace_back("compare", JsonValue("eq"));
        test.emplace_back("tag", JsonValue(std::string()));
        break;
    case AnimFactKind::TagSet:
        test.emplace_back("has", JsonValue("any"));
        test.emplace_back("query", JsonValue(JsonValue::Array{}));
        break;
    }
    return JsonValue(std::move(test));
}

JsonValue MakeAnimTagSetTest(std::string fact, std::string tag)
{
    return JsonValue(JsonValue::Object{
        { "fact", JsonValue(std::move(fact)) },
        { "has", JsonValue("any") },
        { "query", JsonValue(JsonValue::Array{ JsonValue(std::move(tag)) }) },
    });
}

JsonValue MakeAnimRequestTest(std::string intent)
{
    return JsonValue(JsonValue::Object{ { "request", JsonValue(std::move(intent)) } });
}

JsonValue MakeAnimElapsedTest(double seconds)
{
    return JsonValue(JsonValue::Object{
        { "elapsed", JsonValue("behavior") },
        { "compare", JsonValue("gt") },
        { "value", JsonValue(seconds) },
    });
}
