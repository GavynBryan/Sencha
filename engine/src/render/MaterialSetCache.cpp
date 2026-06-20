#include <render/MaterialSetCache.h>

#include <render/MaterialCache.h>

namespace
{
    // Content key for dedup: member count then each handle's token, so distinct
    // lists never collide and the same list always matches (count guards against
    // a shorter list being a prefix of a longer one).
    std::string MakeKey(std::span<const MaterialHandle> materials)
    {
        std::string key = std::to_string(materials.size());
        for (const MaterialHandle handle : materials)
        {
            key.push_back('|');
            key += std::to_string(handle.ToToken());
        }
        return key;
    }
}

MaterialSetCache::MaterialSetCache(MaterialCache* materials)
    : Materials(materials)
{
    ReserveNullSlot();
}

MaterialSetCache::~MaterialSetCache()
{
    FreeAllEntries();
}

MaterialSetHandle MaterialSetCache::Acquire(std::span<const MaterialHandle> materials)
{
    const std::string key = MakeKey(materials);

    // Existing set: ref-count it and return. Done before building an entry so
    // the member retains below only run on a genuine first sight.
    if (MaterialSetHandle existing = FindRegisteredHandle(key, /*addRef*/ true); existing.IsValid())
        return existing;

    MaterialSetEntry entry;
    entry.Materials.assign(materials.begin(), materials.end());
    entry.Alive = true;
    if (Materials != nullptr)
        for (const MaterialHandle handle : entry.Materials)
            Materials->Retain(handle);

    return AllocNamedHandle(key, std::move(entry));
}

const std::vector<MaterialHandle>* MaterialSetCache::Get(MaterialSetHandle handle) const
{
    const MaterialSetEntry* entry = Resolve(handle);
    return entry ? &entry->Materials : nullptr;
}

void MaterialSetCache::OnFree(MaterialSetEntry& entry)
{
    if (Materials != nullptr)
        for (const MaterialHandle handle : entry.Materials)
            Materials->Release(handle);
    entry.Materials.clear();
    entry.Alive = false;
}
