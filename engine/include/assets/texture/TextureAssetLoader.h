#pragma once

#include <core/assets/AssetLoader.h>
#include <core/logging/Logger.h>
#include <render/TextureHandle.h>

class LoggingProvider;
class TextureCache;

//=============================================================================
// TextureAssetLoader
//
// Staged-load contract for textures (docs/assets/pipeline.md, Decision C).
// Stage: bytes -> decoded CPU Image for loose image data, or TextureData for
// cooked .stex. Commit: GPU upload + bindless slot via TextureCache.
//
// The srgb parameter exists only for the Stage 1 loose-PNG mapping, where
// usage is unknowable from the bytes; the generic LoadStaged assumes sRGB
// (base color). Cooked .stex carries a usage tag that replaces the
// parameter entirely (Decisions E/L).
//=============================================================================
class TextureAssetLoader final : public IAssetLoader
{
public:
    TextureAssetLoader(LoggingProvider& logging, TextureCache* cache);

    [[nodiscard]] AssetType Type() const override { return AssetType::Texture; }
    [[nodiscard]] AssetStaging LoadStaged(const AssetRecord& record,
                                          IAssetSource& source) override;
    AssetCommitResult Commit(AssetStaging&& staged) override;

    [[nodiscard]] AssetStaging LoadStaged(const AssetRecord& record,
                                          IAssetSource& source,
                                          bool srgb);

    // Owner-thread commit returning the typed handle (refcount 1, owned by
    // the caller). The virtual Commit wraps this for heterogeneous drivers.
    [[nodiscard]] TextureHandle CommitTyped(AssetStaging&& staged);

    // Owner-thread hot-reload commit (Stage 6): swaps the staged payload into
    // the existing resident entry in place (same bindless slot, no new
    // handle), retiring the old image. Returns false if the texture is not
    // resident — there is nothing to reload, and the refreshed registry hash
    // means the next normal load picks up the new bytes.
    [[nodiscard]] bool CommitReload(AssetStaging&& staged);

private:
    Logger& Log;
    TextureCache* Cache = nullptr;
};
