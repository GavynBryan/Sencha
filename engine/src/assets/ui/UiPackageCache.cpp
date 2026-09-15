#include <assets/ui/UiPackageCache.h>

#include <utility>

UiPackageCache::UiPackageCache(LoggingProvider& logging)
    : Log(logging.GetLogger<UiPackageCache>())
{
    ReserveNullSlot();
}

UiPackageCache::~UiPackageCache()
{
    FreeAllEntries();
}

UiPackageHandle UiPackageCache::Register(std::string_view path, UiPackage package)
{
    if (!package.IsValid())
    {
        Log.Error("UiPackageCache: refusing to register invalid package '{}'", path);
        return {};
    }

    UiPackageEntry entry{};
    entry.Package = std::move(package);
    return AllocNamedHandle(path, std::move(entry));
}

UiPackageHandle UiPackageCache::Find(std::string_view path) const
{
    return FindRegisteredHandle(path);
}

std::string_view UiPackageCache::GetName(UiPackageHandle handle) const
{
    return GetRegisteredPath(handle);
}

const UiPackage* UiPackageCache::Get(UiPackageHandle handle) const
{
    const UiPackageEntry* entry = Resolve(handle);
    return entry != nullptr ? &entry->Package : nullptr;
}

bool UiPackageCache::ReloadInPlace(std::string_view path, UiPackage package)
{
    if (!package.IsValid())
    {
        Log.Error("UiPackageCache: refusing to reload '{}' with an invalid package", path);
        return false;
    }

    const UiPackageHandle handle = FindRegisteredHandle(path);
    UiPackageEntry* entry = handle.IsValid() ? Resolve(handle) : nullptr;
    if (entry == nullptr)
        return false;

    entry->Package = std::move(package);
    ++entry->ReloadVersion;
    return true;
}

std::uint64_t UiPackageCache::GetReloadVersion(UiPackageHandle handle) const
{
    const UiPackageEntry* entry = Resolve(handle);
    return entry != nullptr ? entry->ReloadVersion : 0;
}

// -- AssetCache CRTP hooks ---------------------------------------------------

void UiPackageCache::OnFree(UiPackageEntry& entry)
{
    entry.Package = {};
}

bool UiPackageCache::IsEntryLive(const UiPackageEntry& entry) const
{
    return entry.Package.IsValid();
}
