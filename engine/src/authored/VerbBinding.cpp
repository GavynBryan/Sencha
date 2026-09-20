#include <authored/VerbBinding.h>

// A library holds one file's worth of bindings -- a handful, not a table worth
// indexing. Lookups happen when a consumer is composed or a binding asset
// reloads, never per frame: the compiled binding is what a producer keeps.

const VerbBindingDesc* VerbBindingLibrary::Find(std::string_view key) const
{
    for (const VerbBindingDesc& binding : Bindings)
    {
        if (binding.Key == key)
            return &binding;
    }
    return nullptr;
}

const VerbBindingDesc* VerbBindingLibrary::Find(VerbBindingKey key) const
{
    for (const VerbBindingDesc& binding : Bindings)
    {
        if (binding.KeyId == key)
            return &binding;
    }
    return nullptr;
}
