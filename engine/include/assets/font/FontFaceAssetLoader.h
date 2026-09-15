#pragma once

#include <assets/font/FontFaceCache.h>
#include <core/assets/AssetStager.h>
#include <core/logging/Logger.h>

class LoggingProvider;

//=============================================================================
// FontFaceAssetLoader
//
// Staged load for cooked font faces. Stage: bytes -> FontFace. Commit:
// register with FontFaceCache. Payload type: FontFace.
//
// CPU-only both halves. No reload operation yet: swapping face bytes under a
// document engine that has already ingested and cached the face changes
// nothing visible, so the reload contract lands with document reconstruction
// rather than ahead of it (docs/ui/architecture.md).
//=============================================================================
class FontFaceAssetLoader final : public IAssetStager
{
public:
    FontFaceAssetLoader(LoggingProvider& logging, FontFaceCache* cache);

    [[nodiscard]] AssetStaging LoadStaged(const AssetRecord& record,
                                          IAssetSource& source) override;

    [[nodiscard]] FontFaceHandle CommitTyped(AssetStaging&& staged);

private:
    Logger& Log;
    FontFaceCache* Cache = nullptr;
};
