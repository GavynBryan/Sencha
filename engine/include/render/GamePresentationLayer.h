#pragma once

#include <assets/data/DataAssetHandle.h>
#include <render/PresentationSettings.h>

struct RuntimeAssets;

// The game's own layer, from kGamePresentationAsset when the project has one.
class GamePresentationLayer
{
public:
    explicit GamePresentationLayer(RuntimeAssets& assets);

    // Null without the asset. Follows a reload of it.
    [[nodiscard]] const PresentationSettingsLayer* Get() const;

private:
    RuntimeAssets& Assets;
    DataAssetCacheHandle Lease;
};
