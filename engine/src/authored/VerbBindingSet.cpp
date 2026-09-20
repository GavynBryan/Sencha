#include <authored/VerbBindingSet.h>

#include <utility>

void VerbBindingSet::Instantiate(const VerbBindingLibrary& library,
                                 const VerbBindingEnvironment& environment,
                                 std::vector<std::string>& errors)
{
    Bindings.clear();
    Bindings.reserve(library.Bindings.size());

    for (const VerbBindingDesc& desc : library.Bindings)
    {
        CompiledVerbBinding compiled;
        if (CompileVerbBinding(desc, environment, compiled, errors))
            Bindings.push_back(std::move(compiled));
    }
}

void VerbBindingSet::Append(const VerbBindingLibrary& library,
                            const VerbBindingEnvironment& environment,
                            std::vector<std::string>& errors)
{
    for (const VerbBindingDesc& desc : library.Bindings)
    {
        if (Find(desc.KeyId) != nullptr)
        {
            errors.push_back("binding '" + desc.Key + "' is already in this set");
            continue;
        }
        CompiledVerbBinding compiled;
        if (CompileVerbBinding(desc, environment, compiled, errors))
            Bindings.push_back(std::move(compiled));
    }
}

const CompiledVerbBinding* VerbBindingSet::Find(VerbBindingKey key) const
{
    if (!key.IsValid())
        return nullptr;
    for (const CompiledVerbBinding& binding : Bindings)
    {
        if (binding.Key == key)
            return &binding;
    }
    return nullptr;
}

const CompiledVerbBinding* VerbBindingSet::Find(std::string_view key) const
{
    return Find(MakeVerbBindingKey(key));
}
