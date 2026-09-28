#pragma once

#include <core/assets/AssetRef.h>

#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class AssetRegistry;
class IAssetSource;

// The project's assets the animation panels offer, by kind and, for data, by subtype.
class AnimationContentLists
{
public:
    void Refresh(const AssetRegistry& registry, IAssetSource& source);
    [[nodiscard]] std::span<const std::string> Of(AssetType type) const;
    [[nodiscard]] std::span<const std::string> OfSubtype(std::string_view subtype) const;

private:
    std::map<AssetType, std::vector<std::string>> ByType;
    std::map<std::string, std::vector<std::string>, std::less<>> BySubtype;
};
