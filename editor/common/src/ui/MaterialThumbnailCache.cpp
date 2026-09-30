#include "MaterialThumbnailCache.h"

#include <assets/material/MaterialLoader.h>
#include <assets/runtime/AssetSystem.h>
#include <assets/texture/TextureCache.h>
#include <core/assets/AssetRegistry.h>
#include <graphics/vulkan/UiTextureResidency.h>
#include <ui/UiService.h>

#include <utility>
#include <vector>

MaterialThumbnailCache::MaterialThumbnailCache(AssetSystem& assets,
                                               TextureCache& textures,
                                               VulkanImageService& images,
                                               VulkanSamplerCache& samplers,
                                               const AssetRegistry& registry,
                                               UiService* ui,
                                               UiTextureResidency* uiTextures)
    : Assets(assets)
    , Textures(textures)
    , Images(images)
    , Samplers(samplers)
    , Registry(registry)
    , Ui(ui)
    , UiTextures(uiTextures)
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
    if (entry.Image != kInvalidUiHostImage && UiTextures != nullptr)
        UiTextures->WithdrawHostImage(entry.Image);
    if (entry.Binding != nullptr || entry.Texture.IsValid())
        RetireList.push_back(Retiring{ std::move(entry.Binding), std::move(entry.Texture), Retirement.Stamp() });
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

MaterialThumbnailCache::Entry& MaterialThumbnailCache::Find(const std::string& materialPath)
{
    auto it = Entries.find(materialPath);
    if (it == Entries.end())
    {
        Entry entry;
        entry.TexturePath = ResolveBaseColor(materialPath);
        it = Entries.emplace(materialPath, std::move(entry)).first;
    }
    it->second.LastUsedFrame = Frame;
    return it->second;
}

ImTextureID MaterialThumbnailCache::Thumbnail(const std::string& materialPath)
{
    Entry& entry = Find(materialPath);
    if (entry.TexturePath.empty())
        return ImTextureID{};
    if (entry.Binding == nullptr)
    {
        entry.Binding = std::make_unique<ImGuiTextureBinding>(Assets, Textures, Images, Samplers);
        entry.Binding->SetTexture(entry.TexturePath);
    }
    return entry.Binding->TextureId();
}

std::string MaterialThumbnailCache::ImageName(const std::string& materialPath)
{
    Entry& entry = Find(materialPath);
    if (entry.TexturePath.empty() || Ui == nullptr || UiTextures == nullptr)
        return {};
    std::string name = "material:" + materialPath;
    if (!entry.Texture.IsValid())
    {
        entry.Texture = Assets.LoadLease(entry.TexturePath, AssetType::Texture);
        // Registered for as long as the process runs: the document engine keeps
        // a texture per source, so a name re-registered under a new id would
        // leave it drawing the old one.
        entry.Image = Ui->SetHostImage(name, RenderExtent{ 1, 1 });
    }
    // A hot reload repoints the same slot, so publishing is only ever waiting
    // for the first upload.
    const BindlessImageIndex slot = Textures.GetBindlessIndex(TextureHandle::FromToken(entry.Texture.OpaqueToken()));
    if (slot.IsValid())
        UiTextures->PublishHostImage(entry.Image, slot);
    return name;
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
