#pragma once

#include <anim/AnimationClipCache.h>
#include <anim/AnimationClipHandle.h>
#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>
#include <ecs/ComponentTraits.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/World.h>

#include <cstdint>
#include <string_view>
#include <tuple>

//=============================================================================
// AnimationClipPlayerComponent
//
// Plays one clip on the entity's skinned mesh: the pose source the skinning
// path was built without. Time advances on fixed ticks
// (AnimationClipPlaybackSystem) and the render extract samples whatever
// time the component currently holds, so playback is deterministic under
// catch-up frames and a paused player is a fixed, authorable pose.
//
// Deliberately one clip and no blending. A state graph, transitions, and
// layered modifiers are the animation runtime's business; this is the
// smallest thing that makes a character move, and the seam it leaves is the
// component that a graph would replace rather than extend.
//=============================================================================
struct AnimationClipPlayerComponent
{
    AnimationClipHandle Clip;
    // Where playback currently sits, in seconds from the clip's start.
    float TimeSeconds = 0.0f;
    // Playback speed multiplier. Zero freezes the pose at TimeSeconds, which
    // is how a scene authors a fixed pose (and how a golden capture stays
    // deterministic under a wall-clock tick accumulator).
    float Rate = 1.0f;
    bool Loop = true;
};

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

// Stated rather than derived from TypeSchema::Name, so the schema can move
// without the identity moving with it. The name is repeated exactly.
SENCHA_DECLARE_COMPONENT_TYPE(AnimationClipPlayerComponent, "AnimationClipPlayer");
