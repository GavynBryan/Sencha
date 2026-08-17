#include "MaterialThumbnailCache.h"

#include <assets/material/MaterialLoader.h>
#include <core/assets/AssetRegistry.h>

#include <utility>
#include <vector>

MaterialThumbnailCache::MaterialThumbnailCache(AssetSystem& assets,
                                               TextureCache& textures,
                                               VulkanImageService& images,
                                               VulkanSamplerCache& samplers,
                                               const AssetRegistry& registry)
    : Assets(assets)
    , Textures(textures)
    , Images(images)
    , Samplers(samplers)
    , Registry(registry)
{
}

void MaterialThumbnailCache::BeginFrame(GpuFrameRetirement retirement)
{
    ++Frame;
    Retirement = retirement;

    for (auto it = RetireList.begin(); it != RetireList.end();)
    {
        if (Retirement.IsRetired(it->Stamp))
            it = RetireList.erase(it);
        else
            ++it;
    }
}

void MaterialThumbnailCache::Retire(Entry& entry)
{
    if (entry.Binding != nullptr)
        RetireList.push_back(Retiring{ std::move(entry.Binding), Retirement.Stamp() });
}

std::string MaterialThumbnailCache::ResolveBaseColor(const std::string& materialPath) const
{
    const AssetRecord* record = Registry.FindByPath(materialPath);
    if (record == nullptr || record->FilePath.empty())
        return {};
    MaterialDescription description;
    if (!LoadMaterialFromFile(record->FilePath, description))
        return {};
    return description.BaseColorTexture.Path;
}

ImTextureID MaterialThumbnailCache::Thumbnail(const std::string& materialPath)
{
    auto it = Entries.find(materialPath);
    if (it == Entries.end())
    {
        Entry entry;
        const std::string texturePath = ResolveBaseColor(materialPath);
        if (!texturePath.empty())
        {
            entry.Binding = std::make_unique<ImGuiTextureBinding>(Assets, Textures, Images, Samplers);
            entry.Binding->SetTexture(texturePath);
        }
        it = Entries.emplace(materialPath, std::move(entry)).first;
    }
    it->second.LastUsedFrame = Frame;
    return it->second.Binding != nullptr ? it->second.Binding->TextureId() : ImTextureID{};
}

void MaterialThumbnailCache::TrimToBudget(std::size_t budget)
{
    if (Entries.size() <= budget)
        return;

    std::vector<std::string> keys;
    std::vector<std::uint64_t> lastUsed;
    keys.reserve(Entries.size());
    lastUsed.reserve(Entries.size());
    for (const auto& [path, entry] : Entries)
    {
        keys.push_back(path);
        lastUsed.push_back(entry.LastUsedFrame);
    }

    for (std::size_t index : SelectThumbnailEvictions(lastUsed, Frame, budget))
    {
        const auto it = Entries.find(keys[index]);
        if (it == Entries.end())
            continue;
        Retire(it->second);
        Entries.erase(it);
    }
}

void MaterialThumbnailCache::Clear()
{
    for (auto& [path, entry] : Entries)
        Retire(entry);
    Entries.clear();
}
