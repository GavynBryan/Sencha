#pragma once

class DataAssetCache;

//=============================================================================
// MovementComponentAssets
//
// Where a movement component's authored profile lives, so the component's
// lifecycle hooks can hold what it names. Null in a world composed without
// structured data -- a bare test world, a registry opened only to inspect
// structure -- where the hooks then do nothing.
//
// Its own header because the hosts that point it at a cache are not the code
// that reads it: a game or an editor wiring up its asset stack should not have
// to pull in the movement schemas to do so.
//=============================================================================
struct MovementComponentAssets
{
    MovementComponentAssets() = default;
    explicit MovementComponentAssets(DataAssetCache* profiles)
        : Profiles(profiles)
    {
    }

    DataAssetCache* Profiles = nullptr;
};
