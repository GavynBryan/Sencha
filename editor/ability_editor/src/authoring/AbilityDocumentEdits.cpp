#include "authoring/AbilityDocumentEdits.h"

#include "data/DataPath.h"
#include "data/JsonObjectEdit.h"
#include "identity/RandomKey.h"

#include <algorithm>
#include <utility>

namespace
{
JsonValue::Array& ListIn(JsonValue& object, std::string_view member)
{
    JsonValue* list = FindMember(object, member);
    if (list == nullptr || !list->IsArray())
    {
        SetMember(object, member, JsonValue(JsonValue::Array{}));
        list = FindMember(object, member);
    }
    return list->AsArray();
}

JsonValue* PhaseNamed(JsonValue& data, std::string_view key)
{
    JsonValue* phases = FindMember(data, "phases");
    if (phases == nullptr || !phases->IsArray())
        return nullptr;
    for (JsonValue& phase : phases->AsArray())
        if (ReadMemberString(phase, "key") == key)
            return &phase;
    return nullptr;
}

JsonValue::Array& OpsIn(JsonValue& phase, AbilityOpList list)
{
    if (list != AbilityOpList::While)
        return ListIn(phase, AbilityOpListName(list));
    JsonValue* pulse = FindMember(phase, "while");
    if (pulse == nullptr || !pulse->IsObject())
    {
        SetMember(phase, "while", JsonValue(JsonValue::Object{ { "every", JsonValue(1) } }));
        pulse = FindMember(phase, "while");
    }
    return ListIn(*pulse, "ops");
}

JsonValue Keyed(JsonValue value)
{
    if (!value.IsObject())
        value = JsonValue(JsonValue::Object{});
    SetMember(value, "key", JsonValue(MintStableKey()));
    return value;
}

// Where a keyed element sits: the array holding it and its index, or the
// object whose member it is.
struct Holder
{
    JsonValue* List = nullptr;
    std::size_t Index = 0;
    JsonValue* Owner = nullptr;
    std::string Member = {};
};

std::optional<Holder> HolderOf(JsonValue& data, std::string_view key)
{
    const std::optional<AbilityElement> element = FindAbilityElement(data, key);
    if (!element)
        return std::nullopt;
    const std::optional<std::string_view> parent = DataPathParent(element->Path);
    JsonValue* container = parent ? FindDataValue(data, *parent) : nullptr;
    if (container == nullptr)
        return std::nullopt;
    if (const std::optional<std::size_t> index = DataPathLastIndex(element->Path); index && container->IsArray())
        return Holder{ .List = container, .Index = *index };
    return Holder{ .Owner = container, .Member = element->List };
}

std::size_t Clamped(std::size_t index, const JsonValue::Array& list)
{
    return std::min(index, list.size());
}
} // namespace

std::string AddAbilityPhase(JsonValue& data, std::string name, std::string_view after)
{
    JsonValue phase = Keyed(JsonValue(JsonValue::Object{ { "name", JsonValue(std::move(name)) } }));
    std::string key = ReadMemberString(phase, "key");
    JsonValue::Array& phases = ListIn(data, "phases");
    const auto previous = std::ranges::find_if(
        phases, [&](const JsonValue& existing) { return ReadMemberString(existing, "key") == after; });
    phases.insert(after.empty() || previous == phases.end() ? phases.end() : previous + 1, std::move(phase));
    return key;
}

std::size_t AbilityTransitionsInto(const JsonValue& data, std::string_view phase)
{
    std::size_t count = 0;
    for (const AbilityElement& element : AbilityElementsOf(data))
        if (const JsonValue* transition = element.Kind == AbilityElementKind::Transition
                                              ? FindDataValue(data, element.Path)
                                              : nullptr)
            count += ReadMemberString(*transition, "to") == phase ? 1 : 0;
    return count;
}

bool RemoveAbilityPhase(JsonValue& data, std::string_view phase)
{
    if (PhaseNamed(data, phase) == nullptr)
        return false;
    for (const AbilityElement& element : AbilityElementsOf(data))
        if (element.Kind == AbilityElementKind::Transition)
            if (JsonValue* transition = FindDataValue(data, element.Path);
                transition != nullptr && ReadMemberString(*transition, "to") == phase)
                SetMember(*transition, "to", JsonValue("end"));
    return RemoveAbilityElement(data, phase);
}

bool MoveAbilityPhase(JsonValue& data, std::string_view phase, std::size_t index)
{
    JsonValue::Array& phases = ListIn(data, "phases");
    const auto found =
        std::ranges::find_if(phases, [&](const JsonValue& existing) { return ReadMemberString(existing, "key") == phase; });
    if (found == phases.end())
        return false;
    JsonValue moved = std::move(*found);
    phases.erase(found);
    phases.insert(phases.begin() + static_cast<std::ptrdiff_t>(Clamped(index, phases)), std::move(moved));
    return true;
}

std::string InsertAbilityOp(JsonValue& data, std::string_view phase, AbilityOpList list, std::size_t index, JsonValue op)
{
    JsonValue* target = PhaseNamed(data, phase);
    if (target == nullptr)
        return {};
    op = Keyed(std::move(op));
    std::string key = ReadMemberString(op, "key");
    JsonValue::Array& ops = OpsIn(*target, list);
    ops.insert(ops.begin() + static_cast<std::ptrdiff_t>(Clamped(index, ops)), std::move(op));
    return key;
}

bool MoveAbilityOp(JsonValue& data, std::string_view op, std::string_view phase, AbilityOpList list, std::size_t index)
{
    const std::optional<AbilityElement> element = FindAbilityElement(data, op);
    if (!element || element->Kind != AbilityElementKind::Op || PhaseNamed(data, phase) == nullptr)
        return false;
    const std::optional<Holder> holder = HolderOf(data, op);
    if (!holder || holder->List == nullptr)
        return false;
    JsonValue::Array& from = holder->List->AsArray();
    JsonValue moved = std::move(from[holder->Index]);
    from.erase(from.begin() + static_cast<std::ptrdiff_t>(holder->Index));
    JsonValue::Array& to = OpsIn(*PhaseNamed(data, phase), list);
    to.insert(to.begin() + static_cast<std::ptrdiff_t>(Clamped(index, to)), std::move(moved));
    return true;
}

std::string AddAbilityTransition(JsonValue& data, std::string_view phase, std::string_view trigger,
                                 std::string_view to)
{
    JsonValue* from = PhaseNamed(data, phase);
    if (from == nullptr)
        return {};
    JsonValue transition = Keyed({});
    if (!trigger.empty())
        SetMember(transition, "on", JsonValue(std::string(trigger)));
    SetMember(transition, "to", JsonValue(std::string(to)));
    std::string key = ReadMemberString(transition, "key");
    ListIn(*from, "then").push_back(std::move(transition));
    return key;
}

std::string AddAbilityLocal(JsonValue& data, std::string name, std::string_view type)
{
    JsonValue local = Keyed(JsonValue(JsonValue::Object{ { "name", JsonValue(std::move(name)) },
                                                         { "type", JsonValue(std::string(type)) } }));
    std::string key = ReadMemberString(local, "key");
    ListIn(data, "locals").push_back(std::move(local));
    return key;
}

std::string SetAbilityGuard(JsonValue& data, std::string_view element, JsonValue condition)
{
    const std::optional<AbilityElement> found = FindAbilityElement(data, element);
    if (!found || (found->Kind != AbilityElementKind::Op && found->Kind != AbilityElementKind::Transition))
        return {};
    JsonValue* guarded = FindDataValue(data, found->Path);
    if (guarded == nullptr)
        return {};
    if (condition.IsNull())
    {
        EraseMember(*guarded, "when");
        return {};
    }
    condition = Keyed(std::move(condition));
    std::string key = ReadMemberString(condition, "key");
    SetMember(*guarded, "when", std::move(condition));
    return key;
}

bool SetAbilityElementMember(JsonValue& data, std::string_view element, std::string_view member, JsonValue value)
{
    const std::optional<AbilityElement> found = FindAbilityElement(data, element);
    JsonValue* held = found ? FindDataValue(data, found->Path) : nullptr;
    if (held == nullptr)
        return false;
    SetMember(*held, member, std::move(value));
    return true;
}

bool RemoveAbilityElement(JsonValue& data, std::string_view element)
{
    const std::optional<Holder> holder = HolderOf(data, element);
    if (!holder)
        return false;
    if (holder->Owner != nullptr)
    {
        EraseMember(*holder->Owner, holder->Member);
        return true;
    }
    JsonValue::Array& list = holder->List->AsArray();
    list.erase(list.begin() + static_cast<std::ptrdiff_t>(holder->Index));
    return true;
}

std::string DuplicateAbilityElement(JsonValue& data, std::string_view element)
{
    const std::optional<Holder> holder = HolderOf(data, element);
    if (!holder || holder->List == nullptr)
        return {};
    JsonValue::Array& list = holder->List->AsArray();
    JsonValue copy = list[holder->Index];
    RemintAbilityKeys(copy);
    std::string key = ReadMemberString(copy, "key");
    list.insert(list.begin() + static_cast<std::ptrdiff_t>(holder->Index + 1), std::move(copy));
    return key;
}

void RemintAbilityKeys(JsonValue& value)
{
    if (value.IsArray())
    {
        for (JsonValue& item : value.AsArray())
            RemintAbilityKeys(item);
        return;
    }
    if (!value.IsObject())
        return;
    for (auto& [name, member] : value.AsObject())
    {
        if (name == "key" && member.IsString())
            member = JsonValue(MintStableKey());
        else
            RemintAbilityKeys(member);
    }
}
