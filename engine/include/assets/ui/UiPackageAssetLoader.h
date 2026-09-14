#pragma once

#include <assets/ui/UiPackageCache.h>
#include <core/assets/AssetStager.h>
#include <core/logging/Logger.h>

class LoggingProvider;

//=============================================================================
// UiPackageAssetLoader
//
// Staged load for cooked UI packages. Stage: bytes -> UiPackage, declaring the
// package's resource table as the staging's dependencies. Commit: register with
// UiPackageCache. Payload type: UiPackage.
//
// Declaring dependencies is the whole reason this kind needs no machinery of
// its own: the preloader already resolves AssetStaging::Dependencies, holds
// them until the dependent commits, and detects cycles. A package's fonts and
// textures are warm before a document built from it opens, and the open screen
// takes its own leases against those warm caches.
//
// No reload operation yet -- see FontFaceAssetLoader for why.
//=============================================================================
class UiPackageAssetLoader final : public IAssetStager
{
public:
    UiPackageAssetLoader(LoggingProvider& logging, UiPackageCache* cache);

    [[nodiscard]] AssetStaging LoadStaged(const AssetRecord& record,
                                          IAssetSource& source) override;

    [[nodiscard]] UiPackageHandle CommitTyped(AssetStaging&& staged);

private:
    Logger& Log;
    UiPackageCache* Cache = nullptr;
};
