#include <assets/data/DataAssetCache.h>

#include <utility>

DataAssetCache::DataAssetCache()
{
    ReserveNullSlot();
}

DataAssetCache::~DataAssetCache()
{
    FreeAllEntries();
}

DataAssetHandle DataAssetCache::Register(std::string_view path,
                                         std::string typeName,
                                         std::shared_ptr<const void> value)
{
    if (path.empty() || typeName.empty() || value == nullptr)
        return {};

    if (DataAssetHandle existing = FindRegisteredHandle(path, true); existing.IsValid())
        return existing;

    DataAssetEntry entry;
    entry.Value = std::move(value);
    entry.TypeName = std::move(typeName);
    entry.ReloadVersion = 1;
    entry.Alive = true;

    DataAssetHandle handle = AllocNamedHandle(path, std::move(entry));
    if (handle.IsValid())
        ++LiveSubtypeCounts[std::string(GetSubtype(handle))];
    return handle;
}

bool DataAssetCache::ReloadInPlace(std::string_view path,
                                   std::string_view typeName,
                                   std::shared_ptr<const void> value)
{
    if (value == nullptr)
        return false;

    DataAssetEntry* entry = Resolve(FindRegisteredHandle(path));
    if (entry == nullptr || entry->TypeName != typeName)
        return false;

    entry->Value = std::move(value);
    ++entry->ReloadVersion;
    if (entry->ReloadVersion == 0)
        entry->ReloadVersion = 1;
    return true;
}

DataAssetHandle DataAssetCache::Acquire(std::string_view path)
{
    return FindRegisteredHandle(path, true);
}

DataAssetCacheHandle DataAssetCache::AcquireOwned(std::string_view path)
{
    DataAssetHandle handle = Acquire(path);
    if (!handle.IsValid())
        return {};

    return DataAssetCacheHandle(this, handle, DataAssetCacheHandle::NoAttach);
}

DataAssetHandle DataAssetCache::Find(std::string_view path) const
{
    return FindRegisteredHandle(path);
}

const void* DataAssetCache::GetRaw(DataAssetHandle handle) const
{
    const DataAssetEntry* entry = Resolve(handle);
    return entry ? entry->Value.get() : nullptr;
}

std::string_view DataAssetCache::GetSubtype(DataAssetHandle handle) const
{
    const DataAssetEntry* entry = Resolve(handle);
    return entry ? std::string_view(entry->TypeName) : std::string_view{};
}

uint64_t DataAssetCache::GetReloadVersion(DataAssetHandle handle) const
{
    const DataAssetEntry* entry = Resolve(handle);
    return entry ? entry->ReloadVersion : 0;
}

std::string_view DataAssetCache::GetName(DataAssetHandle handle) const
{
    return GetRegisteredPath(handle);
}

bool DataAssetCache::HasResidentSubtype(std::string_view typeName) const
{
    const auto it = LiveSubtypeCounts.find(std::string(typeName));
    return it != LiveSubtypeCounts.end() && it->second > 0;
}

void DataAssetCache::OnFree(DataAssetEntry& entry)
{
    auto it = LiveSubtypeCounts.find(entry.TypeName);
    if (it != LiveSubtypeCounts.end())
    {
        if (it->second > 1)
            --it->second;
        else
            LiveSubtypeCounts.erase(it);
    }

    entry.Value.reset();
    entry.TypeName.clear();
    entry.ReloadVersion = 0;
    entry.Alive = false;
}

bool DataAssetCache::IsEntryLive(const DataAssetEntry& entry) const
{
    return entry.Alive;
}
