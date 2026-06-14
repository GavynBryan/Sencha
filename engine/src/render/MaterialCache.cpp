#include <render/MaterialCache.h>

MaterialCache::MaterialCache()
{
    ReserveNullSlot();
}

MaterialCache::~MaterialCache()
{
    FreeAllEntries();
}

MaterialHandle MaterialCache::Create(const Material& material)
{
    MaterialEntry entry;
    entry.Value = material;
    entry.Alive = true;
    return AllocHandle(std::move(entry));
}

MaterialHandle MaterialCache::Register(std::string_view name, const Material& material)
{
    if (name.empty())
        return Create(material);

    if (MaterialHandle existing = FindRegisteredHandle(name); existing.IsValid())
        return existing;

    MaterialEntry entry;
    entry.Value = material;
    entry.Alive = true;
    return AllocNamedHandle(name, std::move(entry));
}

MaterialHandle MaterialCache::Register(std::string_view name,
                                       const Material& material,
                                       std::vector<TextureCacheHandle> ownedTextures)
{
    if (!name.empty())
    {
        // Existing entry already owns its own references; ownedTextures is
        // released on return.
        if (MaterialHandle existing = FindRegisteredHandle(name); existing.IsValid())
            return existing;
    }

    MaterialEntry entry;
    entry.Value = material;
    entry.OwnedTextures = std::move(ownedTextures);
    entry.Alive = true;
    return name.empty() ? AllocHandle(std::move(entry)) : AllocNamedHandle(name, std::move(entry));
}

MaterialHandle MaterialCache::Acquire(std::string_view name)
{
    return FindRegisteredHandle(name, true);
}

MaterialCacheHandle MaterialCache::AcquireOwned(std::string_view name)
{
    MaterialHandle handle = Acquire(name);
    if (!handle.IsValid())
        return {};

    return MaterialCacheHandle(this, handle, MaterialCacheHandle::NoAttach);
}

MaterialHandle MaterialCache::Find(std::string_view name) const
{
    return FindRegisteredHandle(name);
}

void MaterialCache::Destroy(MaterialHandle handle)
{
    Release(handle);
}

bool MaterialCache::ReloadInPlace(std::string_view path,
                                  const Material& material,
                                  std::vector<TextureCacheHandle> ownedTextures)
{
    MaterialEntry* entry = Resolve(FindRegisteredHandle(path));
    if (entry == nullptr)
        return false; // not resident; ownedTextures releases as this returns

    // Adopt the new value and texture references; replacing OwnedTextures runs
    // the old vector's destructors, releasing the previously-held texture
    // refs. Generation, refcount, and the handle are untouched.
    entry->Value = material;
    entry->OwnedTextures = std::move(ownedTextures);
    return true;
}

const Material* MaterialCache::Get(MaterialHandle handle) const
{
    const MaterialEntry* entry = Resolve(handle);
    return entry ? &entry->Value : nullptr;
}

std::string_view MaterialCache::GetName(MaterialHandle handle) const
{
    return GetRegisteredPath(handle);
}

bool MaterialCache::OnLoad(std::string_view, MaterialEntry&)
{
    return false;
}

void MaterialCache::OnFree(MaterialEntry& entry)
{
    entry.Value = {};
    entry.OwnedTextures.clear();
    entry.Alive = false;
}

bool MaterialCache::IsEntryLive(const MaterialEntry& entry) const
{
    return entry.Alive;
}
