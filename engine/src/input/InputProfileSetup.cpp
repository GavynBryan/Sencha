#include <input/InputProfileSetup.h>

#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetLease.h>
#include <core/logging/Logger.h>
#include <ecs/World.h>
#include <input/InputBindingCache.h>
#include <input/InputProfileData.h>

namespace
{
    DataAssetCacheHandle Acquire(RuntimeAssets& assets, std::string_view path, Logger& log)
    {
        AssetLease lease = assets.Assets.LoadLease(path, AssetType::Data);
        if (!lease.IsValid())
        {
            log.Error("input: '{}' did not load", path);
            return {};
        }
        return DataAssetCacheHandle(&assets.DataAssets,
                                    DataAssetHandle::FromToken(lease.OpaqueToken()));
    }
}

InputActionId InputProfileLease::Require(std::string_view action, Logger& log) const
{
    const InputActionId id = Actions != nullptr ? Actions->Find(action) : InputActionId{};
    if (!id.IsValid())
        log.Error("input: the action set declares no '{}'", action);
    return id;
}

void InputProfileLease::Reset()
{
    Context.Reset();
    Actions = nullptr;
    Handle = InputProfileHandle{};
    ActionSet.Reset();
    Profile.Reset();
}

InputProfileLease BindInputProfile(World& world, RuntimeAssets& assets,
                                   std::string_view profilePath, std::string_view context,
                                   Logger& log)
{
    InputProfileLease bound;
    bound.Profile = Acquire(assets, profilePath, log);
    if (!bound.Profile.IsValid())
        return bound;

    const CompiledInputProfile* profile = assets.DataAssets.TryGet<CompiledInputProfile>(
        bound.Profile.GetToken(), kInputProfileTypeName);
    if (profile == nullptr)
    {
        log.Error("input: '{}' is not an input profile", profilePath);
        bound.Reset();
        return bound;
    }

    // The action set is the profile's declared dependency. Loaded here because
    // a synchronous load of the profile does not load what it depends on, and
    // the binding cache resolves the set from residency alone.
    bound.ActionSet = Acquire(assets, profile->ActionSetPath, log);
    if (!bound.ActionSet.IsValid())
    {
        bound.Reset();
        return bound;
    }

    bound.Handle = InputProfileHandle{ bound.Profile.GetToken() };
    RegisterInputMapping(world, assets.DataAssets, bound.Handle);
    InputBindingCache& bindings = world.GetResource<InputBindingCache>();
    bound.Actions = bindings.GetActions(bound.Handle);
    if (bound.Actions == nullptr)
    {
        log.Error("input: profile '{}' did not bind: {}", profilePath,
                  DescribeBindErrors(bindings.Status(bound.Handle)));
        bound.Reset();
        return bound;
    }

    bound.Context = world.GetResource<InputContextSet>().Activate(std::string(context));
    return bound;
}
