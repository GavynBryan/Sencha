#include <assets/scene/SceneCache.h>

#include <utility>

SceneCache::SceneCache(LoggingProvider& logging)
    : Log(logging.GetLogger<SceneCache>())
{
    ReserveNullSlot();
}

SceneCache::~SceneCache()
{
    FreeAllEntries();
}

SceneHandle SceneCache::Register(std::string_view path, SmapContents contents)
{
    SceneEntry entry{};
    entry.Contents = std::make_shared<const SmapContents>(std::move(contents));
    return AllocNamedHandle(path, std::move(entry));
}

SceneHandle SceneCache::Find(std::string_view path) const
{
    return FindRegisteredHandle(path);
}

std::string_view SceneCache::GetName(SceneHandle handle) const
{
    return GetRegisteredPath(handle);
}

const SmapContents* SceneCache::Get(SceneHandle handle) const
{
    const SceneEntry* entry = Resolve(handle);
    return entry != nullptr ? entry->Contents.get() : nullptr;
}

std::shared_ptr<const SmapContents> SceneCache::GetShared(SceneHandle handle) const
{
    const SceneEntry* entry = Resolve(handle);
    return entry != nullptr ? entry->Contents : nullptr;
}

// -- AssetCache CRTP hooks ---------------------------------------------------

void SceneCache::OnFree(SceneEntry& entry)
{
    entry.Contents.reset();
}

bool SceneCache::IsEntryLive(const SceneEntry& entry) const
{
    return entry.Contents != nullptr;
}
