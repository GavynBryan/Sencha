#include <input/InputActionRegistry.h>

#include <unordered_set>

bool InputActionRegistry::Rebuild(std::span<const InputActionDefinition> definitions,
                                  std::string* error)
{
    // Checked before anything is written, so a rejected set leaves the previous
    // vocabulary intact for the caller to keep using.
    std::unordered_set<std::string_view> declared;
    declared.reserve(definitions.size());
    for (const InputActionDefinition& definition : definitions)
    {
        if (definition.Name.empty())
        {
            if (error != nullptr)
                *error = "input action name is empty";
            return false;
        }
        if (!declared.insert(definition.Name).second)
        {
            if (error != nullptr)
                *error = "duplicate input action '" + definition.Name + "'";
            return false;
        }
    }

    // Retire everything, then revive what the set still declares. Doing it in
    // that order means a name's state comes from one place rather than from a
    // diff against the previous set.
    for (Slot& slot : Slots)
        slot.State = SlotState::Retired;

    for (const InputActionDefinition& definition : definitions)
    {
        if (const auto existing = IdsByName.find(definition.Name); existing != IdsByName.end())
        {
            Slot& slot = Slots[IndexOf(existing->second)];
            slot.Definition = definition;
            slot.State = SlotState::Live;
            continue;
        }

        const InputActionId id{ static_cast<std::uint32_t>(Slots.size() + 1) };
        IdsByName.emplace(definition.Name, id);
        Slots.push_back(Slot{ definition, SlotState::Live });
    }

    return true;
}

InputActionId InputActionRegistry::Find(std::string_view name) const
{
    const auto it = IdsByName.find(std::string(name));
    if (it == IdsByName.end() || !IsLive(it->second))
        return InputActionId{};
    return it->second;
}

bool InputActionRegistry::IsLive(InputActionId id) const
{
    if (!id.IsValid() || id.Value > Slots.size())
        return false;
    return Slots[IndexOf(id)].State == SlotState::Live;
}

const InputActionDefinition* InputActionRegistry::Get(InputActionId id) const
{
    if (!IsLive(id))
        return nullptr;
    return &Slots[IndexOf(id)].Definition;
}
