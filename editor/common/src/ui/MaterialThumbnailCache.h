#pragma once

#include "ThumbnailEviction.h"

#include <core/assets/AssetLease.h>
#include <graphics/GpuFrameRetirement.h>
#include <render/ui/UiDrawFrame.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class AssetRegistry;
class AssetSystem;
class ConsoleRegistry;
class Engine;
class TextureCache;
class UiService;
class UiTextureResidency;

//=============================================================================
// MaterialThumbnailCache: pictures of materials and textures as host images
// an authored document names. A material is pictured by its base color texture
// (the slot parsed from the .smat source on first request), a texture by
// itself. Residency is bounded by TrimToBudget, so a large library can be
// scrolled without holding every texture on the GPU: only pictures an
// explorer showed recently stay resident.
//
// Limits: the budget caps the picture COUNT, not bytes, and each picture
// samples the full texture (there is no thumbnail-sized cooked artifact yet;
// producing one would slot into the base-color resolve step).
//=============================================================================
class MaterialThumbnailCache
{
public:
    // Host image names start with `imagePrefix`, so two caches never share one.
    MaterialThumbnailCache(AssetSystem& assets,
                           TextureCache& textures,
                           const AssetRegistry& registry,
                           UiService* ui,
                           UiTextureResidency* uiTextures,
                           std::string imagePrefix = "material:");
    // editor.materials.thumbnail_budget, for every cache; registering it again
    // changes nothing.
    static void RegisterBudget(ConsoleRegistry& registry);
    // Once per frame, before any picture is asked for: BeginFrame with the
    // renderer's retirement, then TrimToBudget to the budget cvar.
    void UpdateFrame(Engine& engine);

    // Advances the LRU clock; once per frame before any picture is asked for.
    void BeginFrame(GpuFrameRetirement retirement);

    // The host image a document names a material's picture by, as `host:NAME`;
    // published while the texture is resident. Empty for a texture-less
    // material, or when there is no authored UI. Marks the entry used.
    [[nodiscard]] std::string ImageName(const std::string& materialPath);
    // The same, for a texture pictured by itself.
    [[nodiscard]] std::string TextureImageName(const std::string& texturePath);

    // Evicts least-recently-used pictures until at most `budget` remain
    // (entries shown this frame are kept regardless).
    void TrimToBudget(std::size_t budget);

    // Drops every entry; the next request re-parses its .smat. Needed after a
    // rescan (a save may have re-pointed a material's base color slot); texture
    // CONTENT hot reload needs nothing here, the bindless slot is repointed.
    void Clear();

    [[nodiscard]] std::size_t ResidentCount() const { return Entries.size(); }

private:
    struct Entry
    {
        // Empty when the material resolved to "no base color texture" (or
        // failed to parse); the negative result is cached so scrolling does not
        // re-parse the .smat every frame.
        std::string TexturePath;
        AssetLease Texture;
        UiHostImageId Image = kInvalidUiHostImage;
        std::uint64_t LastUsedFrame = 0;
    };

    // An evicted texture kept resident until a frame that may still sample it
    // through its host image has retired.
    struct Retiring
    {
        AssetLease Texture;
        std::uint64_t Stamp = 0;
    };

    // The material's base color texture path; empty when the material has none
    // or the .smat does not resolve/parse.
    [[nodiscard]] std::string ResolveBaseColor(const std::string& materialPath) const;

    // The entry under `key`, made with `texturePath` the first time it is asked for.
    template <typename TexturePath>
    Entry& Find(const std::string& key, TexturePath&& texturePath);
    std::string Publish(const std::string& key, Entry& entry);
    // Withdraws the entry's image and moves what it holds into the retire
    // list; the caller then erases the entry.
    void Retire(Entry& entry);

    std::string ImagePrefix;
    AssetSystem& Assets;
    TextureCache& Textures;
    const AssetRegistry& Registry;
    UiService* Ui = nullptr;
    UiTextureResidency* UiTextures = nullptr;

    std::unordered_map<std::string, Entry> Entries;
    std::vector<Retiring> RetireList;
    GpuFrameRetirement Retirement;
    std::uint64_t Frame = 0;
};
