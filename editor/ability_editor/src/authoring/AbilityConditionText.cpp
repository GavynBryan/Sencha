#include "authoring/AbilityConditionText.h"

#include "data/JsonObjectEdit.h"

#include <array>
#include <format>
#include <string_view>
#include <utility>

namespace
{
constexpr std::array<std::pair<std::string_view, std::string_view>, 6> kComparisons{ {
    { "at_least", "\xE2\x89\xA5" },
    { "at_most", "\xE2\x89\xA4" },
    { "above", ">" },
    { "below", "<" },
    { "equals", "=" },
    { "not_equals", "\xE2\x89\xA0" },
} };

std::string Number(const JsonValue& value)
{
    return value.IsNumber() ? std::format("{}", value.AsNumber()) : std::string("?");
}

std::string Compared(const JsonValue& body)
{
    std::string text;
    for (const auto& [member, symbol] : kComparisons)
        if (const JsonValue* bound = FindMember(body, member))
            text += std::format("{}{} {}", text.empty() ? "" : ", ", symbol, Number(*bound));
    return text;
}

std::string LocalName(const JsonValue& data, std::string_view key)
{
    if (const JsonValue* locals = FindMember(data, "locals"); locals != nullptr && locals->IsArray())
        for (const JsonValue& local : locals->AsArray())
            if (ReadMemberString(local, "key") == key)
            {
                const std::string name = ReadMemberString(local, "name");
                return name.empty() ? std::string(key) : name;
            }
    return std::string(key);
}

// An argument-shaped value: a local (and field), a port, or a constant.
std::string ValueText(const JsonValue& data, const JsonValue& value)
{
    if (const std::string local = ReadMemberString(value, "local"); !local.empty())
    {
        const std::string field = ReadMemberString(value, "field");
        return field.empty() ? LocalName(data, local) : std::format("{}.{}", LocalName(data, local), field);
    }
    if (const std::string port = ReadMemberString(value, "port"); !port.empty())
        return port;
    if (const JsonValue* constant = FindMember(value, "const"))
        return Number(*constant);
    return "?";
}

std::string Names(const JsonValue* list)
{
    std::string text;
    if (list != nullptr && list->IsArray())
        for (const JsonValue& name : list->AsArray())
            if (name.IsString())
                text += std::format("{}{}", text.empty() ? "" : ", ", name.AsString());
    return text;
}

std::string Tags(const JsonValue& query)
{
    std::string text;
    const auto add = [&](std::string_view lead, const JsonValue* list) {
        if (const std::string names = Names(list); !names.empty())
            text += std::format("{}{}{}", text.empty() ? "" : "; ", lead, names);
    };
    add("has ", FindMember(query, "all"));
    add("any of ", FindMember(query, "any"));
    add("not ", FindMember(query, "none"));
    return text.empty() ? std::string("any tags") : text;
}

std::string Joined(const JsonValue& data, const JsonValue* children, std::string_view separator)
{
    std::string text;
    if (children != nullptr && children->IsArray())
        for (const JsonValue& child : children->AsArray())
            text += std::format("{}{}", text.empty() ? "" : separator, AbilityConditionText(data, child));
    return std::format("({})", text);
}
} // namespace

std::string AbilityConditionText(const JsonValue& data, const JsonValue& condition)
{
    if (const JsonValue* body = FindMember(condition, "tags"))
        return Tags(*body);
    if (const JsonValue* body = FindMember(condition, "attribute"))
        return std::format("{} {}", ReadMemberString(*body, "name"), Compared(*body));
    if (const JsonValue* body = FindMember(condition, "local"))
        return std::format("{} {}", LocalName(data, ReadMemberString(*body, "of")), Compared(*body));
    if (const JsonValue* body = FindMember(condition, "hit"); body != nullptr && body->IsString())
        return std::format("{} struck", LocalName(data, body->AsString()));
    if (const JsonValue* body = FindMember(condition, "entity"))
    {
        const JsonValue* of = FindMember(*body, "of");
        std::string text = std::format("entity({})", of != nullptr ? ValueText(data, *of) : std::string("?"));
        if (const std::string has = Names(FindMember(*body, "has")); !has.empty())
            text += " has " + has;
        if (const JsonValue* tags = FindMember(*body, "tags"))
            text += " " + Tags(*tags);
        return text;
    }
    if (const JsonValue* body = FindMember(condition, "phase_ticks"))
        return std::format("phase ticks {}", Compared(*body));
    if (const JsonValue* body = FindMember(condition, "pulse"))
        return std::format("pulse {}", Compared(*body));
    if (const JsonValue* body = FindMember(condition, "input"); body != nullptr && body->IsString())
        return body->AsString() == "held" ? "input held" : "input not held";
    if (const JsonValue* body = FindMember(condition, "query"))
        return std::format("query {}", ReadMemberString(*body, "name"));
    if (const JsonValue* children = FindMember(condition, "all"))
        return "all " + Joined(data, children, " and ");
    if (const JsonValue* children = FindMember(condition, "any"))
        return "any " + Joined(data, children, " or ");
    if (const JsonValue* child = FindMember(condition, "not"))
        return std::format("not ({})", AbilityConditionText(data, *child));
    return "condition";
}
