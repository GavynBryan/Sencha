#include <anim/AnimRootMotionSource.h>

#include <anim/AnimContentState.h>
#include <anim/AnimRequests.h>
#include <anim/AnimRig.h>
#include <anim/AnimRigBinding.h>
#include <anim/AnimRootMotion.h>
#include <anim/AnimationClipCache.h>
#include <ecs/World.h>
#include <movement/RootMotionSource.h>
#include <world/transform/TransformComponents.h>

#include <algorithm>

namespace
{
    // What carried the character on a tick.
    struct RootMotionPlayback
    {
        const AnimationClipData* Clip = nullptr;
        AnimPlayback Time;
    };

    const AnimationClipData* ClipOf(const AnimBoundRig& rig, const AnimationClipCache& clips, std::size_t content)
    {
        if (content >= rig.Contents.size() || !rig.Contents[content].IsClip())
            return nullptr;
        return clips.Get(rig.Contents[content].Clip);
    }

    // What carried the character on any tick, past ones included, is the newest live
    // request whose behavior carries: named by the request on a request-keyed layer, by
    // the rule reading it on a selector's (docs/gameplay/animation.md, "Root motion").
    bool PlaybackFromRequests(const AnimBoundRig& rig, const AnimationClipCache& clips, const AnimRequestSet& requests,
                              const AnimLayerContent& base, AnimTick tick, RootMotionPlayback& out)
    {
        const std::vector<AnimBoundRule>* rules =
            rig.Layers[0].Selector >= 0 ? &rig.Selectors[static_cast<std::size_t>(rig.Layers[0].Selector)].Rules : nullptr;
        const auto carrierOf = [&](GameplayTagId intent) -> const AnimBoundBehavior* {
            if (rules == nullptr)
            {
                const AnimBoundBehavior* behavior = rig.FindBehavior(intent);
                return behavior != nullptr && behavior->Policy.RootMotion ? behavior : nullptr;
            }
            for (const AnimBoundRule& rule : *rules)
            {
                if (rule.LatchIntent != intent)
                    continue;
                const AnimBoundBehavior* behavior = rig.FindBehavior(rule.Behavior);
                if (behavior != nullptr && behavior->Policy.RootMotion)
                    return behavior;
            }
            return nullptr;
        };
        const AnimRequest* carrying = nullptr;
        const AnimBoundBehavior* carrier = nullptr;
        for (const AnimRequest& request : requests.Records)
        {
            if ((request.Layers & 1u) == 0 || !WasAnimRequestLive(request, tick))
                continue;
            const AnimBoundBehavior* behavior = carrierOf(request.Intent);
            if (behavior == nullptr)
                continue;
            if (carrying == nullptr || IsNewerAnimRequest(request, *carrying))
            {
                carrying = &request;
                carrier = behavior;
            }
        }
        if (carrying == nullptr)
            return false;

        // The row the layer resolved, when it is playing this request;
        // otherwise the behavior's first clip row.
        const AnimationClipData* clip = base.Request == carrying->Id ? ClipOf(rig, clips, base.Clip) : nullptr;
        for (std::size_t r = 0; clip == nullptr && r < rig.SlotRows.size(); ++r)
            if (rig.SlotRows[r].Behavior == carrier->Tag && rig.SlotRows[r].Content >= 0)
                clip = ClipOf(rig, clips, static_cast<std::size_t>(rig.SlotRows[r].Content));
        if (clip == nullptr)
            return false;
        out = RootMotionPlayback{ .Clip = clip,
                                  .Time = AnimPlayback{ .StartTick = carrying->StartTick,
                                                        .OffsetSeconds = carrier->Policy.StartSeconds,
                                                        .Rate = carrier->Policy.Rate,
                                                        .DurationSeconds = clip->DurationSeconds,
                                                        .Cyclic = carrier->Policy.Kind == AnimBehaviorKind::Cyclic } };
        return true;
    }
}

bool SampleAnimRootMotion(World& world, EntityId entity, std::uint64_t tick, double tickSeconds,
                          RootMotionSample& out)
{
    const World& reader = world;
    if (!reader.IsRegistered<AnimRig>() || !reader.IsRegistered<AnimContentState>() || tickSeconds <= 0.0)
        return false;
    const AnimRig* rig = reader.TryGet<AnimRig>(entity);
    const AnimContentState* content = reader.TryGet<AnimContentState>(entity);
    AnimRigBindings* bindings = world.TryGetResource<AnimRigBindings>();
    if (rig == nullptr || content == nullptr || bindings == nullptr || bindings->ClipSource() == nullptr)
        return false;
    const AnimBoundRig* bound = bindings->Resolve(rig->Rig, world);
    if (bound == nullptr || !bound->Valid || bound->Layers.empty())
        return false;

    // The base layer carries the character; a layer over it animates in place.
    const AnimLayerContent& base = content->Layers[0];
    const AnimRequestSet* requests =
        reader.IsRegistered<AnimRequestSet>() ? reader.TryGet<AnimRequestSet>(entity) : nullptr;
    RootMotionPlayback playback;
    const bool carried =
        requests != nullptr && PlaybackFromRequests(*bound, *bindings->ClipSource(), *requests, base, tick, playback);
    if (!carried || !playback.Clip->Root.has_value())
        return false;

    // The tick covers the step from the one before; nothing before the clip
    // began carries anyone. Played backwards the clip carries nothing: a
    // root curve says where it goes forwards.
    const double to = AnimElapsedSeconds(playback.Time, tick, tickSeconds);
    const double from = tick > 0 ? AnimElapsedSeconds(playback.Time, tick - 1, tickSeconds) : to;
    const AnimRootDelta delta =
        AnimRootMotionBetween(*playback.Clip->Root, playback.Clip->DurationSeconds, from, to, playback.Time.Cyclic);

    // The skeleton's model space is the character's own: turned by its facing.
    const LocalTransform* transform =
        reader.IsRegistered<LocalTransform>() ? reader.TryGet<LocalTransform>(entity) : nullptr;
    const Vec3d local{ delta.X, 0.0f, delta.Z };
    const Vec3d moved = transform != nullptr ? transform->Value.Rotation.RotateVector(local) : local;
    out.PlanarVelocity = moved * static_cast<float>(1.0 / tickSeconds);
    out.TurnRadians = delta.Yaw;
    return true;
}
