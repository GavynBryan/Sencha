#include <render/GamePresentationLayer.h>

#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetLease.h>
#include <core/assets/AssetRegistry.h>

#include <string>

GamePresentationLayer::GamePresentationLayer(RuntimeAssets& assets)
    : Assets(assets)
{
    // A game without the asset has no layer of its own, which is not a failure.
    if (Assets.Registry.Find(AssetRef{ .Path = std::string(kGamePresentationAsset) }) == nullptr)
        return;
    const AssetLease lease = Assets.Assets.LoadLease(kGamePresentationAsset, AssetType::Data);
    if (lease.IsValid())
        Lease = DataAssetCacheHandle(&Assets.DataAssets, DataAssetHandle::FromToken(lease.OpaqueToken()));
}

const PresentationSettingsLayer* GamePresentationLayer::Get() const
{
    return Lease.IsValid()
        ? Assets.DataAssets.TryGet<PresentationSettingsLayer>(Lease.GetToken(), kPresentationSettingsType)
        : nullptr;
}
