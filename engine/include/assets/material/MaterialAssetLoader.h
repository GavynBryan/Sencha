#pragma once

#include <assets/material/MaterialFormat.h>
#include <assets/texture/TextureAssetLoader.h>
#include <core/assets/AssetStager.h>
#include <core/logging/Logger.h>
#include <render/TextureHandle.h>

#include <string_view>
#include <vector>

class AssetSystem;
class LoggingProvider;
class MaterialCache;
class TextureCache;

//=============================================================================
// MaterialAssetLoader
//
// Staged-load contract for .smat (docs/assets/pipeline.md, Decision C).
// Stage: bytes -> MaterialDescription (pure JSON parse). Commit: resolve
// texture slots to bindless indices through the asset front door, register
// the runtime Material with owned texture references. Payload type:
// MaterialDescription.
//
// Materials are the first payload that references other assets: commit
// loads its textures through the front door it is handed, which on the
// synchronous path may decode inline and on the manifest-driven path hits the
// cache because the manifest staged them first. That dependency is real and
// will recur (Decision O: templates and tables reference assets too).
//=============================================================================
class MaterialAssetLoader final : public IAssetStager
{
public:
    MaterialAssetLoader(LoggingProvider& logging,
                        MaterialCache* materials,
                        TextureCache* textures);

    [[nodiscard]] AssetStaging LoadStaged(const AssetRecord& record,
                                          IAssetSource& source) override;

    // Owner-thread commit returning the typed handle (refcount 1, owned by
    // the caller). `assets` resolves the description's texture references.
    [[nodiscard]] MaterialHandle CommitTyped(AssetStaging&& staged, AssetSystem& assets);

    // Owner-thread hot-reload commit: re-resolves the staged description
    // (re-resolving its texture slots, which may load a newly referenced
    // texture) and swaps it into the existing resident entry in place,
    // keeping the handle and releasing the previously-owned texture refs.
    // Returns false if the material is not resident.
    [[nodiscard]] bool CommitReload(AssetStaging&& staged, AssetSystem& assets);

private:
    // Builds the runtime Material from a parsed description, resolving each
    // texture slot to a bindless index and accumulating the owned texture
    // references into `outOwned`. Shared by CommitTyped and CommitReload.
    [[nodiscard]] Material ResolveDescription(const MaterialDescription& desc,
                                              AssetSystem& assets,
                                              std::vector<TextureCacheHandle>& outOwned);

    void ResolveTextureSlot(const AssetRef& ref,
                            bool srgb,
                            AssetSystem& assets,
                            uint32_t& outIndex,
                            std::vector<TextureCacheHandle>& owned);

    // One reference to the texture at `path`, staged with `srgb` when it is
    // not yet resident. The generic texture load assumes sRGB; only a
    // material knows which of its slots hold color.
    [[nodiscard]] TextureHandle LoadTexture(std::string_view path, bool srgb, AssetSystem& assets);

    Logger& Log;
    MaterialCache* Materials = nullptr;
    TextureCache* Textures = nullptr;
    TextureAssetLoader TextureLoader;
};
