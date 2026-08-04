#include <assets/data/DataAssetTypeRegistry.h>

#include <algorithm>
#include <utility>

bool DataAssetTypeRegistry::Register(DataAssetTypeRegistration registration)
{
    if (registration.Name.empty() || registration.CurrentVersion == 0
        || !registration.Compile || Find(registration.Name) != nullptr)
    {
        return false;
    }

    Registrations.push_back(std::move(registration));
    return true;
}

bool DataAssetTypeRegistry::Unregister(std::string_view name)
{
    auto it = std::find_if(Registrations.begin(), Registrations.end(),
        [name](const DataAssetTypeRegistration& registration)
        {
            return registration.Name == name;
        });
    if (it == Registrations.end())
        return false;
    if (HasResidentValues && HasResidentValues(name))
        return false;

    Registrations.erase(it);
    return true;
}

const DataAssetTypeRegistration* DataAssetTypeRegistry::Find(std::string_view name) const
{
    const auto it = std::find_if(Registrations.begin(), Registrations.end(),
        [name](const DataAssetTypeRegistration& registration)
        {
            return registration.Name == name;
        });
    return it == Registrations.end() ? nullptr : &*it;
}

std::span<const DataAssetTypeRegistration> DataAssetTypeRegistry::Entries() const
{
    return Registrations;
}
