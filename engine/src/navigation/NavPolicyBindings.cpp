#include "NavPolicyBindings.h"

#include <assets/data/DataAssetCache.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <navigation/NavigationPolicyData.h>

const NavQueryPolicy* NavPolicyBindings::Bind(DataAssetHandle handle, const GameplayTagRegistry& tags)
{
    if (DataAssets == nullptr || !handle.IsValid())
        return nullptr;
    Entry& entry = Entries[handle.ToToken()];
    const std::uint64_t reload = DataAssets->GetReloadVersion(handle);
    if (entry.Policy == nullptr || entry.ReloadVersion != reload || entry.TagCount != tags.Size())
    {
        const NavigationPolicyData* data =
            DataAssets->TryGet<NavigationPolicyData>(handle, kNavigationPolicySubtype);
        entry.ReloadVersion = reload;
        entry.TagCount = tags.Size();
        entry.Resident = data != nullptr;
        entry.Unresolved.clear();
        if (entry.Policy == nullptr)
            entry.Policy = std::make_unique<NavQueryPolicy>();
        *entry.Policy = data != nullptr ? BindNavigationPolicy(*data, tags, &entry.Unresolved) : NavQueryPolicy{};
    }
    return entry.Resident ? entry.Policy.get() : nullptr;
}

std::size_t NavPolicyBindings::UnresolvedNames() const
{
    std::size_t count = 0;
    for (const auto& [handle, entry] : Entries)
        count += entry.Unresolved.size();
    return count;
}
