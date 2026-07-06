#pragma once

#include "ImGuiTextureBinding.h"
#include "ThumbnailEviction.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class AssetRegistry;
class AssetSystem;
class TextureCache;
class VulkanImageService;
class VulkanSamplerCache;

//=============================================================================
// MaterialThumbnailCache: GPU-resident preview images for material assets,
// keyed by .smat virtual path. A thumbnail is the material's base color
// texture (slot parsed from the .smat source on first request), displayed
// through an ImGuiTextureBinding. Residency is bounded by TrimToBudget, so a
// large library can be scrolled without holding every texture on the GPU:
// only thumbnails a panel actually drew recently stay resident.
//
// Limits: the budget caps the thumbnail COUNT, not bytes, and each thumbnail
// samples the full base color texture (there is no thumbnail-sized cooked
// artifact yet; producing one would slot into the base-color resolve step).
//
// Teardown follows ImGuiTextureBinding's contract: destroy or Clear() while
// the asset system and the ImGui Vulkan backend are both alive.
//=============================================================================
class MaterialThumbnailCache
{
public:
    MaterialThumbnailCache(AssetSystem& assets,
                           TextureCache& textures,
                           VulkanImageService& images,
                           VulkanSamplerCache& samplers,
                           const AssetRegistry& registry);

    // Advances the LRU clock; call once per UI frame before any Thumbnail().
    void BeginFrame();

    // The ImGui texture id for the material's base color, loading it on first
    // request; 0 while unresolved or for a texture-less material (the caller
    // draws a placeholder). Marks the entry used this frame.
    [[nodiscard]] ImTextureID Thumbnail(const std::string& materialPath);

    // Evicts least-recently-used thumbnails until at most `budget` remain
    // (entries drawn this frame are kept regardless).
    void TrimToBudget(std::size_t budget);

    // Drops every entry; the next Thumbnail() re-parses its .smat. Needed after
    // a rescan (a save may have re-pointed a material's base color slot);
    // texture CONTENT hot reload needs nothing here, the bindings rebind
    // swapped GPU images themselves.
    void Clear();

    [[nodiscard]] std::size_t ResidentCount() const { return Entries.size(); }

private:
    struct Entry
    {
        // Null when the material resolved to "no base color texture" (or failed
        // to parse); the negative result is cached so scrolling does not
        // re-parse the .smat every frame.
        std::unique_ptr<ImGuiTextureBinding> Binding;
        std::uint64_t LastUsedFrame = 0;
    };

    // An evicted binding kept alive a few frames before destruction. Its
    // descriptor set may still be sampled by an in-flight command buffer
    // (ImGuiTextureBinding::Release frees sets immediately, assuming the owner
    // has stopped drawing them), so eviction and Clear defer the destroy past
    // frames-in-flight instead of dropping the binding inline.
    struct Retiring
    {
        std::unique_ptr<ImGuiTextureBinding> Binding;
        int FramesLeft = 0;
    };

    // The material's base color texture path; empty when the material has none
    // or the .smat does not resolve/parse.
    [[nodiscard]] std::string ResolveBaseColor(const std::string& materialPath) const;

    // Moves the map entry's binding (if any) into the retire list; the caller
    // then erases the entry.
    void Retire(Entry& entry);

    AssetSystem& Assets;
    TextureCache& Textures;
    VulkanImageService& Images;
    VulkanSamplerCache& Samplers;
    const AssetRegistry& Registry;

    std::unordered_map<std::string, Entry> Entries;
    std::vector<Retiring> RetireList;
    std::uint64_t Frame = 0;
};
