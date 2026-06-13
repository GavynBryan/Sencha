#include <anim/AnimationClipCache.h>

AnimationClipCache::AnimationClipCache()
{
    ReserveNullSlot();
}

AnimationClipCache::~AnimationClipCache()
{
    FreeAllEntries();
}

AnimationClipHandle AnimationClipCache::Register(std::string_view name,
                                                 AnimationClipData clip,
                                                 SkeletonCacheHandle ownedSkeleton)
{
    if (name.empty())
        return {};

    // Existing entry already owns its own skeleton reference; ownedSkeleton
    // releases on return.
    if (AnimationClipHandle existing = FindRegisteredHandle(name); existing.IsValid())
        return existing;

    AnimationClipEntry entry;
    entry.Value = std::move(clip);
    entry.OwnedSkeleton = std::move(ownedSkeleton);
    entry.Alive = true;
    return AllocNamedHandle(name, std::move(entry));
}

AnimationClipHandle AnimationClipCache::Acquire(std::string_view name)
{
    return FindRegisteredHandle(name, true);
}

AnimationClipCacheHandle AnimationClipCache::AcquireOwned(std::string_view name)
{
    AnimationClipHandle handle = Acquire(name);
    if (!handle.IsValid())
        return {};
    return AnimationClipCacheHandle(this, handle, AnimationClipCacheHandle::NoAttach);
}

AnimationClipHandle AnimationClipCache::Find(std::string_view name) const
{
    return FindRegisteredHandle(name);
}

const AnimationClipData* AnimationClipCache::Get(AnimationClipHandle handle) const
{
    const AnimationClipEntry* entry = Resolve(handle);
    return entry ? &entry->Value : nullptr;
}

std::string_view AnimationClipCache::GetName(AnimationClipHandle handle) const
{
    return GetRegisteredPath(handle);
}

bool AnimationClipCache::OnLoad(std::string_view, AnimationClipEntry&)
{
    // No file IO in caches (Decision I): clips arrive through Register,
    // fed by the staged loader. Acquire on an unknown name simply misses.
    return false;
}

void AnimationClipCache::OnFree(AnimationClipEntry& entry)
{
    entry.Value = {};
    entry.OwnedSkeleton.Reset(); // releases the skeleton reference
    entry.Alive = false;
}

bool AnimationClipCache::IsEntryLive(const AnimationClipEntry& entry) const
{
    return entry.Alive;
}
