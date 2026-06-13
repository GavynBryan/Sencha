#include <anim/SkeletonCache.h>

SkeletonCache::SkeletonCache()
{
    ReserveNullSlot();
}

SkeletonCache::~SkeletonCache()
{
    FreeAllEntries();
}

SkeletonHandle SkeletonCache::Register(std::string_view name, SkeletonData skeleton)
{
    if (name.empty())
        return {};

    if (SkeletonHandle existing = FindRegisteredHandle(name); existing.IsValid())
        return existing;

    SkeletonEntry entry;
    entry.Value = std::move(skeleton);
    entry.Alive = true;
    return AllocNamedHandle(name, std::move(entry));
}

SkeletonHandle SkeletonCache::Acquire(std::string_view name)
{
    return FindRegisteredHandle(name, true);
}

SkeletonCacheHandle SkeletonCache::AcquireOwned(std::string_view name)
{
    SkeletonHandle handle = Acquire(name);
    if (!handle.IsValid())
        return {};
    return SkeletonCacheHandle(this, handle, SkeletonCacheHandle::NoAttach);
}

SkeletonHandle SkeletonCache::Find(std::string_view name) const
{
    return FindRegisteredHandle(name);
}

const SkeletonData* SkeletonCache::Get(SkeletonHandle handle) const
{
    const SkeletonEntry* entry = Resolve(handle);
    return entry ? &entry->Value : nullptr;
}

std::string_view SkeletonCache::GetName(SkeletonHandle handle) const
{
    return GetRegisteredPath(handle);
}

bool SkeletonCache::OnLoad(std::string_view, SkeletonEntry&)
{
    // No file IO in caches (Decision I): skeletons arrive through Register,
    // fed by the staged loader. Acquire on an unknown name simply misses.
    return false;
}

void SkeletonCache::OnFree(SkeletonEntry& entry)
{
    entry.Value = {};
    entry.Alive = false;
}

bool SkeletonCache::IsEntryLive(const SkeletonEntry& entry) const
{
    return entry.Alive;
}
