#include "MaterialThumbnailCache.h"

#include <app/Engine.h>
#include <assets/material/MaterialLoader.h>
#include <assets/runtime/AssetSystem.h>
#include <assets/texture/TextureCache.h>
#include <core/assets/AssetRegistry.h>
#include <core/console/ConsoleRegistry.h>
#include <core/console/ConsoleService.h>
#include <graphics/vulkan/GraphicsServices.h>
#include <graphics/vulkan/UiTextureResidency.h>
#include <ui/UiService.h>

#include <algorithm>
#include <format>
#include <utility>
#include <vector>

MaterialThumbnailCache::MaterialThumbnailCache(AssetSystem& assets,
                                               TextureCache& textures,
                                               const AssetRegistry& registry,
                                               UiService* ui,
                                               UiTextureResidency* uiTextures,
                                               std::string imagePrefix)
    : ImagePrefix(std::move(imagePrefix))
    , Assets(assets)
    , Textures(textures)
    , Registry(registry)
    , Ui(ui)
    , UiTextures(uiTextures)
{
}

namespace
{
constexpr const char* kBudgetCVar = "editor.materials.thumbnail_budget";
}

void MaterialThumbnailCache::RegisterBudget(ConsoleRegistry& registry)
{
    if (registry.FindCVar(kBudgetCVar) != nullptr)
        return;
    registry.RegisterCVar({
        .Name = kBudgetCVar,
        .Owner = "editor",
        .Type = CVarType::Int,
        .DefaultValue = std::int64_t{ 128 },
        .CurrentValue = std::int64_t{ 128 },
        .Flags = CVarFlags::Archive,
        .Help = "Most material thumbnails kept on the GPU; the ones shown most recently stay.",
        .Source = { "editor" },
    });
}

void MaterialThumbnailCache::UpdateFrame(Engine& engine)
{
    // The renderer's fence-anchored clock decides when an evicted binding's
    // descriptor set is safe to free.
    GpuFrameRetirement retirement;
    if (const GraphicsServices* graphics = engine.TryGraphics())
        retirement = graphics->Frames.GetRetirement();
    BeginFrame(retirement);
    std::int64_t budget = 128;
    if (const CVarMetadata* var = engine.Console().Registry().FindCVar(kBudgetCVar))
        if (const std::int64_t* value = std::get_if<std::int64_t>(&var->CurrentValue))
            budget = *value;
    TrimToBudget(static_cast<std::size_t>(std::max<std::int64_t>(1, budget)));
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
    if (entry.Texture.IsValid())
        RetireList.push_back(Retiring{ std::move(entry.Texture), Retirement.Stamp() });
}

void MaterialThumbnailCache::ResolveBaseColor(const std::string& materialPath, Entry& entry) const
{
    const AssetRecord* record = Registry.FindByPath(materialPath);
    MaterialDescription description;
    if (record == nullptr || record->FilePath.empty() || !LoadMaterialFromFile(record->FilePath, description))
        return;
    entry.TexturePath = description.BaseColorTexture.Path;
    const Vec4& color = description.BaseColorFactor;
    const auto channel = [](float value) { return static_cast<int>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f); };
    entry.Swatch = std::format("rgba({}, {}, {}, 255)", channel(color.X), channel(color.Y), channel(color.Z));
}

template <typename Resolve>
MaterialThumbnailCache::Entry& MaterialThumbnailCache::Find(const std::string& key, Resolve&& resolve)
{
    auto it = Entries.find(key);
    if (it == Entries.end())
    {
        Entry entry;
        resolve(entry);
        it = Entries.emplace(key, std::move(entry)).first;
    }
    it->second.LastUsedFrame = Frame;
    return it->second;
}

std::string MaterialThumbnailCache::ImageName(const std::string& materialPath)
{
    return Publish(materialPath, Find(materialPath, [&](Entry& entry) { ResolveBaseColor(materialPath, entry); }));
}

std::string MaterialThumbnailCache::TextureImageName(const std::string& texturePath)
{
    const std::string key = "texture:" + texturePath;
    return Publish(key, Find(key, [&](Entry& entry) { entry.TexturePath = texturePath; }));
}

AssetPicture MaterialThumbnailCache::PictureOf(std::string_view assetPath)
{
    const std::string path(assetPath);
    if (!path.ends_with(".smat"))
        return { .Image = TextureImageName(path) };
    return { .Image = ImageName(path), .Color = Entries.at(path).Swatch };
}

std::string MaterialThumbnailCache::Publish(const std::string& key, Entry& entry)
{
    if (entry.TexturePath.empty() || Ui == nullptr || UiTextures == nullptr)
        return {};
    std::string name = ImagePrefix + key;
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
