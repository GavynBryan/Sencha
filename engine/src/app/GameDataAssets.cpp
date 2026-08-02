#include <app/GameDataAssets.h>

#include <app/Game.h>
#include <assets/runtime/RuntimeAssets.h>

void RegisterGameDataAssets(Game& game, RuntimeAssets& assets)
{
    game.OnRegisterDataAssetTypes(assets.DataTypes, assets.DataSchemas);
}

void UnregisterGameDataAssets(Game& game, RuntimeAssets& assets)
{
    game.OnUnregisterDataAssetTypes(assets.DataTypes, assets.DataSchemas);
}
