#pragma once

#include <assets/data/DataAssetHandle.h>
#include <input/InputAction.h>
#include <input/InputContextSet.h>
#include <input/InputRegistration.h>

#include <string>
#include <string_view>

class InputActionRegistry;
class Logger;
class World;
struct RuntimeAssets;

//=============================================================================
// What a game holds once its controls are bound: the leases that keep the
// profile and the action set it names resident, the compiled action registry
// to resolve names against, and the context it plays in. Released by Reset or
// destruction, which must happen before the content stack goes.
//=============================================================================
struct InputProfileLease
{
    DataAssetCacheHandle Profile;
    DataAssetCacheHandle ActionSet;
    InputProfileHandle Handle;
    const InputActionRegistry* Actions = nullptr;
    InputContextLease Context;

    [[nodiscard]] bool Ready() const { return Actions != nullptr; }

    // The id of a named action, or invalid with an error logged: an action a
    // game requires and the set does not declare is a content error, not a
    // control that quietly reads zero.
    [[nodiscard]] InputActionId Require(std::string_view action, Logger& log) const;

    void Reset();
};

// Loads `profilePath`, the action set it names, registers the mapping on the
// world, binds it, and activates `context`: everything a game used to do by
// hand at startup, with the dependency the profile declares loaded through the
// same front door rather than by a second path the game had to know about. On
// any failure the result is not Ready, the reason is logged, and whatever was
// acquired is released.
[[nodiscard]] InputProfileLease BindInputProfile(World& world, RuntimeAssets& assets,
                                                 std::string_view profilePath,
                                                 std::string_view context, Logger& log);
