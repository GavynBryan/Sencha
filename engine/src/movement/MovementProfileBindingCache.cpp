#include <movement/MovementProfileBindingCache.h>

#include <gameplay_tags/GameplayTagRegistry.h>
#include <movement/LocomotionMode.h>

#include <utility>

MovementProfileBindingCache::MovementProfileBindingCache(DataAssetCache& dataAssets,
                                                         GameplayTagRegistry& tags,
                                                         LocomotionModeRegistry& modes)
    : DataAssets(dataAssets)
    , Tags(tags)
    , Modes(modes)
{
}

const BoundMovementProfile* MovementProfileBindingCache::Get(MovementProfileHandle handle,
                                                              std::string* newError)
{
    if (newError != nullptr)
        newError->clear();
    if (!handle.IsValid())
    {
        if (newError != nullptr)
            *newError = "movement profile handle is invalid";
        return nullptr;
    }

    const uint64_t key = handle.Value.ToToken();
    Entry& entry = Entries[key];
    if (!entry.Lease)
    {
        const std::string_view path = DataAssets.GetName(handle.Value);
        if (!path.empty())
            entry.Lease = DataAssets.AcquireOwned(path);
    }

    const uint64_t reloadVersion = DataAssets.GetReloadVersion(handle.Value);
    const uint64_t modeGeneration = Modes.Generation();
    const std::size_t tagCount = Tags.Size();
    const bool needsRebuild = entry.ReloadVersion != reloadVersion
        || entry.ModeGeneration != modeGeneration
        || entry.TagCount != tagCount;

    if (needsRebuild)
    {
        ++Rebuilds;
        entry.Profile = {};
        entry.Error.clear();
        entry.ErrorDelivered = false;
        entry.ReloadVersion = reloadVersion;
        entry.ModeGeneration = modeGeneration;
        entry.TagCount = tagCount;

        const CompiledMovementProfile* portable = DataAssets.TryGet<CompiledMovementProfile>(
            handle.Value, "movement.profile");
        if (portable == nullptr)
        {
            entry.Error = "data asset is stale or is not a movement.profile";
        }
        else
        {
            MovementProfileBindResult bound = BindMovementProfile(
                *portable,
                Tags,
                [this](std::string_view name)
                {
                    const LocomotionModeEntry* mode = Modes.Find(name);
                    return mode != nullptr ? mode->Id : LocomotionModeId{};
                });
            if (bound.IsValid())
                entry.Profile = std::move(*bound.Profile);
            else
                entry.Error = bound.Error.empty()
                    ? "movement profile binding failed"
                    : std::move(bound.Error);
        }
    }

    if (!entry.Error.empty() && !entry.ErrorDelivered && newError != nullptr)
    {
        *newError = entry.Error;
        entry.ErrorDelivered = true;
    }

    return &entry.Profile;
}

void MovementProfileBindingCache::Clear()
{
    Entries.clear();
}
