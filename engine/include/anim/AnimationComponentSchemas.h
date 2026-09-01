#pragma once

#include <anim/AnimationClipCache.h>
#include <anim/AnimationClipPlayerComponent.h>
#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>
#include <ecs/ComponentTraits.h>
#include <ecs/EntityId.h>
#include <ecs/World.h>

#include <cstdint>
#include <string_view>
#include <tuple>

//=============================================================================
// Authoring shape and clip lifetime for the animation components.
//
// Registration and the serializers include this. A system that reads one of
// these components includes the component and gets its values, not the
// services that own what the values refer to.
//=============================================================================

// The caches a clip player retains through, published as a world resource
// exactly like SkinnedMeshComponentAssets.
struct AnimationClipComponentAssets
{
    AnimationClipComponentAssets() = default;
    explicit AnimationClipComponentAssets(AnimationClipCache* clips)
        : Clips(clips)
    {
    }

    AnimationClipCache* Clips = nullptr;
};

template <>
struct ComponentTraits<AnimationClipPlayerComponent>
{
    static void OnAdd(AnimationClipPlayerComponent& component, World& world, EntityId)
    {
        auto* assets = world.TryGetResource<AnimationClipComponentAssets>();
        if (assets != nullptr && assets->Clips != nullptr)
            assets->Clips->Retain(component.Clip);
    }

    static void OnRemove(const AnimationClipPlayerComponent& component, World& world, EntityId)
    {
        auto* assets = world.TryGetResource<AnimationClipComponentAssets>();
        if (assets != nullptr && assets->Clips != nullptr)
            assets->Clips->Release(component.Clip);
    }
};

template <>
struct TypeSchema<AnimationClipPlayerComponent>
{
    static constexpr std::string_view Name = "AnimationClipPlayer";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('A', 'C', 'L', 'P');

    static auto Fields()
    {
        const AnimationClipPlayerComponent defaults;
        return std::tuple{
            MakeField("clip", &AnimationClipPlayerComponent::Clip)
                .AsAsset(AssetType::AnimationClip)
                .Label("Animation")
                .Tooltip("The clip this entity plays on its skinned mesh."),
            MakeField("time_seconds", &AnimationClipPlayerComponent::TimeSeconds)
                .Default(defaults.TimeSeconds)
                .Label("Time")
                .Tooltip("Current playback position, in seconds from the "
                         "clip's start."),
            MakeField("rate", &AnimationClipPlayerComponent::Rate)
                .Default(defaults.Rate)
                .Label("Speed")
                .Tooltip("Playback speed multiplier. Zero holds the pose at "
                         "Time, which is how a scene authors a fixed pose."),
            MakeField("loop", &AnimationClipPlayerComponent::Loop)
                .Default(defaults.Loop)
                .Label("Loop")
                .Tooltip("Wraps back to the start at the clip's end; off "
                         "holds the final pose."),
        };
    }
};
