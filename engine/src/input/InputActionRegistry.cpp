#include <input/InputActionRegistry.h>

bool InputActionRegistry::Register(InputActionDefinition definition, std::string* error)
{
    if (definition.Name.empty())
    {
        if (error != nullptr)
            *error = "input action name is empty";
        return false;
    }

    if (IdsByName.find(definition.Name) != IdsByName.end())
    {
        if (error != nullptr)
            *error = "duplicate input action '" + definition.Name + "'";
        return false;
    }

    const InputActionId id{ static_cast<std::uint32_t>(Definitions.size() + 1) };
    IdsByName.emplace(definition.Name, id);
    Definitions.push_back(std::move(definition));
    return true;
}

InputActionId InputActionRegistry::Find(std::string_view name) const
{
    const auto it = IdsByName.find(std::string(name));
    return it == IdsByName.end() ? InputActionId{} : it->second;
}

const InputActionDefinition* InputActionRegistry::Get(InputActionId id) const
{
    if (!id.IsValid() || id.Value > Definitions.size())
        return nullptr;
    return &Definitions[IndexOf(id)];
}
