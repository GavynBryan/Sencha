#include "authoring/AnimationPredicateEdits.h"

void AddAnimPredicateRow(JsonValue::Array& rows, JsonValue test)
{
    rows.push_back(std::move(test));
}

bool RemoveAnimPredicateRow(JsonValue::Array& rows, std::size_t row)
{
    if (row >= rows.size())
        return false;
    rows.erase(rows.begin() + static_cast<std::ptrdiff_t>(row));
    return true;
}

bool AddAnimPredicateAlternative(JsonValue::Array& rows, std::size_t row, JsonValue test)
{
    if (row >= rows.size())
        return false;
    JsonValue& entry = rows[row];
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

bool RemoveAnimPredicateAlternative(JsonValue::Array& rows, std::size_t row, std::size_t alternative)
{
    if (row >= rows.size())
        return false;
    JsonValue* any = rows[row].Find("any");
    if (any == nullptr || !any->IsArray() || alternative >= any->AsArray().size())
        return false;
    JsonValue::Array& tests = any->AsArray();
    tests.erase(tests.begin() + static_cast<std::ptrdiff_t>(alternative));
    // A group of one is just that test.
    if (tests.size() == 1)
    {
        JsonValue remaining = std::move(tests.front());
        rows[row] = std::move(remaining);
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
