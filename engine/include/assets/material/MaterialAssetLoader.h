#pragma once

#include <assets/material/MaterialFormat.h>
#include <core/assets/AssetLoader.h>
#include <core/logging/Logger.h>
#include <render/TextureHandle.h>

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
// Materials are the first payload that references other assets — commit
// loads its textures through AssetSystem, which on the synchronous path may
// decode inline and on the manifest-driven path (Stage 3) hits the cache
// because the manifest staged them first. That dependency is real and will
// recur (Decision O: templates and tables reference assets too).
//=============================================================================
class MaterialAssetLoader final : public IAssetLoader
{
public:
    MaterialAssetLoader(LoggingProvider& logging,
                        AssetSystem& assets,
                        MaterialCache* materials,
                        TextureCache* textures);

    [[nodiscard]] AssetType Type() const override { return AssetType::Material; }
    [[nodiscard]] AssetStaging LoadStaged(const AssetRecord& record,
                                          IAssetSource& source) override;
    AssetCommitResult Commit(AssetStaging&& staged) override;

    // Owner-thread commit returning the typed handle (refcount 1, owned by
    // the caller). The virtual Commit wraps this for heterogeneous drivers.
    [[nodiscard]] MaterialHandle CommitTyped(AssetStaging&& staged);

    // Owner-thread hot-reload commit (Stage 6c): re-resolves the staged
    // description (re-resolving its texture slots, which may load a newly
    // referenced texture) and swaps it into the existing resident entry in
    // place, keeping the handle and releasing the previously-owned texture
    // refs. Returns false if the material is not resident.
    [[nodiscard]] bool CommitReload(AssetStaging&& staged);

private:
    // Builds the runtime Material from a parsed description, resolving each
    // texture slot to a bindless index and accumulating the owned texture
    // references into `outOwned`. Shared by CommitTyped and CommitReload.
    [[nodiscard]] Material ResolveDescription(const MaterialDescription& desc,
                                              std::string_view path,
                                              std::vector<TextureCacheHandle>& outOwned);

    void ResolveTextureSlot(const AssetRef& ref,
                            bool srgb,
                            uint32_t& outIndex,
                            std::vector<TextureCacheHandle>& owned);

    Logger& Log;
    AssetSystem& Assets;
    MaterialCache* Materials = nullptr;
    TextureCache* Textures = nullptr;
};
