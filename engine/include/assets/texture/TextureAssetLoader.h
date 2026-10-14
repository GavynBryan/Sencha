#pragma once

#include <core/assets/AssetStager.h>
#include <core/logging/Logger.h>
#include <render/TextureHandle.h>

class LoggingProvider;
class TextureCache;

// Stages a cooked .stex, or a loose image whose colour space its bytes cannot
// say (LoadStagedAs; the generic LoadStaged assumes sRGB), and commits through
// TextureCache (docs/assets/pipeline.md, Decisions C, E, L).
class TextureAssetLoader final : public IAssetStager
{
public:
    TextureAssetLoader(LoggingProvider& logging, TextureCache* cache);

    [[nodiscard]] AssetStaging LoadStaged(const AssetRecord& record,
                                          IAssetSource& source,
                                          const AssetPathsById& current) override;

    [[nodiscard]] AssetStaging LoadStagedAs(const AssetRecord& record,
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
