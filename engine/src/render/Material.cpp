#include <render/Material.h>

MaterialHandle MaterialStore::Create(const Material& material)
{
    uint32_t index = 0;
    if (!FreeSlots.empty())
    {
        index = FreeSlots.back();
        FreeSlots.pop_back();
    }
    else
    {
        index = static_cast<uint32_t>(Entries.size());
        Entries.emplace_back();
    }

    Entry& entry = Entries[index];
    entry.Value = material;
    entry.Alive = true;
    ++entry.Generation;
    if (entry.Generation == 0) entry.Generation = 1;  // skip 0; IsValid() treats it as null

    return { index, entry.Generation };
}

void MaterialStore::Destroy(MaterialHandle handle)
{
    if (!handle.IsValid() || handle.Index >= Entries.size()) return;
    Entry& entry = Entries[handle.Index];
    if (!entry.Alive || entry.Generation != handle.Generation) return;

    entry.Alive = false;
    FreeSlots.push_back(handle.Index);
}

const Material* MaterialStore::Get(MaterialHandle handle) const
{
    if (!handle.IsValid() || handle.Index >= Entries.size()) return nullptr;
    const Entry& entry = Entries[handle.Index];
    return entry.Alive && entry.Generation == handle.Generation ? &entry.Value : nullptr;
}
