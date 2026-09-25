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
    // What carried the character on a tick: a clip, where it started, and
    // whether it loops.
    struct Carrier
    {
        const AnimationClipData* Clip = nullptr;
        AnimTick StartTick = 0;
        float OffsetSeconds = 0.0f;
        float Rate = 1.0f;
        bool Cyclic = false;
    };

    const AnimationClipData* ClipOf(const AnimBoundRig& rig, const AnimationClipCache& clips, std::size_t content)
    {
        if (content >= rig.Contents.size() || !rig.Contents[content].IsClip())
            return nullptr;
        return clips.Get(rig.Contents[content].Clip);
    }

    // A request-keyed base layer plays whatever request was live, so what
    // carried the character on any tick -- a past one included -- follows from
    // the request records alone: each says when it started and when it was
    // cancelled.
    bool CarrierFromRequests(const AnimBoundRig& rig, const AnimationClipCache& clips, const AnimRequestSet& requests,
                             const AnimLayerContent& base, AnimTick tick, Carrier& out)
    {
        const AnimRequest* carrying = nullptr;
        for (const AnimRequest& request : requests.Records)
        {
            if ((request.Layers & 1u) == 0 || !WasAnimRequestLive(request, tick))
                continue;
            const AnimBoundBehavior* behavior = rig.FindBehavior(request.Intent);
            if (behavior == nullptr || !behavior->Policy.RootMotion)
                continue;
            if (carrying == nullptr || request.StartTick > carrying->StartTick
                || (request.StartTick == carrying->StartTick && request.Id.Sequence > carrying->Id.Sequence))
                carrying = &request;
        }
        if (carrying == nullptr)
            return false;

        // The row the layer resolved, when it is playing this request;
        // otherwise the behavior's first clip row.
        const AnimationClipData* clip = base.Request == carrying->Id ? ClipOf(rig, clips, base.Clip) : nullptr;
        for (std::size_t r = 0; clip == nullptr && r < rig.SlotRows.size(); ++r)
            if (rig.SlotRows[r].Behavior == carrying->Intent && rig.SlotRows[r].Content >= 0)
                clip = ClipOf(rig, clips, static_cast<std::size_t>(rig.SlotRows[r].Content));
        if (clip == nullptr)
            return false;
        const AnimBoundBehavior* behavior = rig.FindBehavior(carrying->Intent);
        out = Carrier{ .Clip = clip,
                       .StartTick = carrying->StartTick,
                       .OffsetSeconds = behavior->Policy.StartSeconds,
                       .Rate = behavior->Policy.Rate,
                       .Cyclic = behavior->Policy.Kind == AnimBehaviorKind::Cyclic };
        return true;
    }

    // A selector's base layer carries what it plays now: selection is state,
    // not a function of the tick.
    bool CarrierFromLayer(const AnimBoundRig& rig, const AnimationClipCache& clips, const AnimLayerContent& base,
                          AnimTick tick, Carrier& out)
    {
        const AnimBoundBehavior* behavior = rig.FindBehavior(base.Behavior);
        if (behavior == nullptr || !behavior->Policy.RootMotion || tick < base.ClipStartTick
            || base.Clip >= rig.Contents.size())
            return false;
        const AnimationClipData* clip = clips.Get(rig.Contents[base.Clip].Clip);
        if (clip == nullptr)
            return false;
        out = Carrier{ .Clip = clip,
                       .StartTick = base.ClipStartTick,
                       .OffsetSeconds = base.ClipOffsetSeconds,
                       .Rate = base.ClipRate,
                       .Cyclic = behavior->Policy.Kind == AnimBehaviorKind::Cyclic };
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
    Carrier carrier;
    const bool carried = bound->Layers[0].Selector < 0
        ? requests != nullptr && CarrierFromRequests(*bound, *bindings->ClipSource(), *requests, base, tick, carrier)
        : CarrierFromLayer(*bound, *bindings->ClipSource(), base, tick, carrier);
    if (!carried || !carrier.Clip->Root.has_value())
        return false;

    const auto elapsed = [&](double at) {
        return static_cast<double>(carrier.OffsetSeconds)
            + std::max(at - static_cast<double>(carrier.StartTick), 0.0) * tickSeconds * static_cast<double>(carrier.Rate);
    };
    // The tick covers the step from the one before; nothing before the clip
    // began carries anyone. Played backwards the clip carries nothing: a
    // root curve says where it goes forwards.
    const double to = elapsed(static_cast<double>(tick));
    const double from = elapsed(static_cast<double>(tick) - 1.0);
    const AnimRootDelta delta =
        AnimRootMotionBetween(*carrier.Clip->Root, carrier.Clip->DurationSeconds, from, to, carrier.Cyclic);

    // The skeleton's model space is the character's own: turned by its facing.
    const LocalTransform* transform =
        reader.IsRegistered<LocalTransform>() ? reader.TryGet<LocalTransform>(entity) : nullptr;
    const Vec3d local{ delta.X, 0.0f, delta.Z };
    const Vec3d moved = transform != nullptr ? transform->Value.Rotation.RotateVector(local) : local;
    out.PlanarVelocity = moved * static_cast<float>(1.0 / tickSeconds);
    out.TurnRadians = delta.Yaw;
    return true;
}
