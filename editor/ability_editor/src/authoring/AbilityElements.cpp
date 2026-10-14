#include "authoring/AbilityElements.h"

#include "authoring/AbilityConditionText.h"

#include "data/DataPath.h"
#include "data/JsonObjectEdit.h"

#include <array>
#include <format>
#include <utility>

namespace
{
constexpr std::array<std::pair<std::string_view, AbilityElementKind>, 13> kHolders{ {
    { "activation", AbilityElementKind::Condition },
    { "continuation", AbilityElementKind::Condition },
    { "cancel_on", AbilityElementKind::Condition },
    { "continue", AbilityElementKind::Condition },
    { "when", AbilityElementKind::Condition },
    { "locals", AbilityElementKind::Local },
    { "costs", AbilityElementKind::CostGroup },
    { "phases", AbilityElementKind::Phase },
    { "enter", AbilityElementKind::Op },
    { "ops", AbilityElementKind::Op },
    { "hold", AbilityElementKind::Op },
    { "exit", AbilityElementKind::Op },
    { "present", AbilityElementKind::Presentation },
} };

constexpr std::array<std::pair<AbilityOpList, std::string_view>, 4> kOpLists{ {
    { AbilityOpList::Enter, "enter" },
    { AbilityOpList::While, "while" },
    { AbilityOpList::Hold, "hold" },
    { AbilityOpList::Exit, "exit" },
} };

std::optional<AbilityElementKind> KindHeldBy(std::string_view member)
{
    if (member == "then")
        return AbilityElementKind::Transition;
    for (const auto& [name, kind] : kHolders)
        if (name == member)
            return kind;
    return std::nullopt;
}

struct Walker
{
    std::vector<AbilityElement>& Out;

    void Value(const JsonValue& value, const std::string& path, std::string_view member, const std::string& phase,
               std::size_t index)
    {
        if (value.IsArray())
        {
            const JsonValue::Array& items = value.AsArray();
            for (std::size_t i = 0; i < items.size(); ++i)
                Value(items[i], std::format("{}[{}]", path, i), member, phase, i);
            return;
        }
        if (!value.IsObject())
            return;
        std::string inside = phase;
        const std::string key = ReadMemberString(value, "key");
        if (const std::optional<AbilityElementKind> kind = KindHeldBy(member); kind && !key.empty())
        {
            Out.push_back({ .Kind = *kind, .Key = key, .Path = path, .Phase = phase, .List = std::string(member),
                            .Index = index });
            if (*kind == AbilityElementKind::Phase)
                inside = key;
        }
        for (const auto& [name, child] : value.AsObject())
            Value(child, path + "." + name, name, inside, 0);
    }
};

std::string NameOrKey(const JsonValue& element)
{
    const std::string name = ReadMemberString(element, "name");
    return name.empty() ? ReadMemberString(element, "key") : name;
}

// The named element of `list` whose key is `key`, by its name.
std::string NameIn(const JsonValue& data, std::string_view list, std::string_view key)
{
    if (const JsonValue* items = FindMember(data, list); items != nullptr && items->IsArray())
        for (const JsonValue& item : items->AsArray())
            if (ReadMemberString(item, "key") == key)
                return NameOrKey(item);
    return std::string(key);
}

std::string OpLabel(const JsonValue& data, const JsonValue& op)
{
    if (const std::string lease = ReadMemberString(op, "lease"); !lease.empty())
        return lease;
    const std::string kind = ReadMemberString(op, "op");
    if (kind == "invoke")
        return ReadMemberString(op, "verb");
    if (kind == "query")
    {
        const std::string into = ReadMemberString(op, "into");
        const std::string query = ReadMemberString(op, "query");
        return into.empty() ? query : std::format("{} \xE2\x86\x92 {}", query, NameIn(data, "locals", into));
    }
    if (kind == "set")
        return std::format("set {}", NameIn(data, "locals", ReadMemberString(op, "local")));
    if (kind == "cue")
        return std::format("cue {}", ReadMemberString(op, "cue"));
    return kind;
}

std::string TransitionLabel(const JsonValue& data, const JsonValue& transition)
{
    const std::string to = ReadMemberString(transition, "to");
    const std::string target = to == "end" ? std::string("end") : NameIn(data, "phases", to);
    const std::string on = ReadMemberString(transition, "on");
    return on.empty() ? std::format("\xE2\x86\x92 {}", target) : std::format("on {} \xE2\x86\x92 {}", on, target);
}
} // namespace

std::string_view AbilityOpListName(AbilityOpList list)
{
    for (const auto& [value, name] : kOpLists)
        if (value == list)
            return name;
    return {};
}

std::optional<AbilityOpList> AbilityOpListNamed(std::string_view name)
{
    for (const auto& [value, listName] : kOpLists)
        if (listName == name)
            return value;
    return std::nullopt;
}

std::vector<AbilityElement> AbilityElementsOf(const JsonValue& data)
{
    std::vector<AbilityElement> elements;
    Walker{ elements }.Value(data, std::string(kDataPathRoot), {}, {}, 0);
    return elements;
}

std::optional<AbilityElement> FindAbilityElement(const JsonValue& data, std::string_view key)
{
    for (AbilityElement& element : AbilityElementsOf(data))
        if (element.Key == key)
            return std::move(element);
    return std::nullopt;
}

std::optional<AbilityElement> AbilityElementAt(const JsonValue& data, std::string_view path)
{
    std::optional<AbilityElement> innermost;
    for (AbilityElement& element : AbilityElementsOf(data))
    {
        const bool under = path.starts_with(element.Path)
            && (path.size() == element.Path.size() || path[element.Path.size()] == '.'
                || path[element.Path.size()] == '[');
        if (under && (!innermost || element.Path.size() > innermost->Path.size()))
            innermost = std::move(element);
    }
    return innermost;
}

std::string AbilityElementLabel(const JsonValue& data, const AbilityElement& element)
{
    const JsonValue* value = FindDataValue(data, element.Path);
    if (value == nullptr)
        return element.Key;
    switch (element.Kind)
    {
    case AbilityElementKind::Op: return OpLabel(data, *value);
    case AbilityElementKind::Transition: return TransitionLabel(data, *value);
    case AbilityElementKind::Presentation: return ReadMemberString(*value, "cue");
    case AbilityElementKind::Condition: return AbilityConditionText(data, *value);
    case AbilityElementKind::Phase:
    case AbilityElementKind::Local:
    case AbilityElementKind::CostGroup: return NameOrKey(*value);
    }
    return element.Key;
}
