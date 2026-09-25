#include <anim/AnimPoseEvaluation.h>

#include <anim/AnimBlendspace.h>
#include <anim/AnimBlendspaceData.h>
#include <anim/AnimContentSystem.h>
#include <anim/AnimPoseComposition.h>
#include <anim/AnimationClipCache.h>
#include <anim/AnimationClipSampling.h>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float kRestDistance = 1e-6f;
    constexpr float kRestAngle = 1e-6f;

    // The shortest rotation's axis and angle in [0, pi]; a zero angle for none.
    // Read from the vector part, so a quaternion a rounding off unit length
    // still gives a unit axis.
    void AxisAngle(Quatf q, Vec3d& axis, float& angle)
    {
        if (q.W < 0.0f)
            q = -q;
        const Vec3d vector(q.X, q.Y, q.Z);
        const float sine = vector.Magnitude();
        angle = 2.0f * std::atan2(sine, q.W);
        if (sine < 1e-9f || angle < kRestAngle)
        {
            axis = Vec3d(1.0f, 0.0f, 0.0f);
            angle = 0.0f;
            return;
        }
        axis = vector / sine;
    }

    // The quintic from x0 with speed v0 to rest at T, with no velocity or
    // acceleration there, evaluated at t.
    float Quintic(float x0, float v0, float T, float t)
    {
        if (t >= T || T <= 0.0f)
            return 0.0f;
        const float a0 = std::max(0.0f, (-8.0f * v0 * T - 20.0f * x0) / (T * T));
        const float T2 = T * T;
        const float T3 = T2 * T;
        const float A = -(a0 * T2 + 6.0f * v0 * T + 12.0f * x0) / (2.0f * T3 * T2);
        const float B = (3.0f * a0 * T2 + 16.0f * v0 * T + 30.0f * x0) / (2.0f * T2 * T2);
        const float C = -(3.0f * a0 * T2 + 12.0f * v0 * T + 20.0f * x0) / (2.0f * T3);
        const float t2 = t * t;
        const float t3 = t2 * t;
        return A * t3 * t2 + B * t2 * t2 + C * t3 + 0.5f * a0 * t2 + v0 * t + x0;
    }

    // A decay that heads the wrong way does not start that way, and one that
    // would overshoot within its time finishes sooner.
    void Settle(float x0, float& v0, float& seconds)
    {
        if (v0 > 0.0f)
            v0 = 0.0f;
        if (v0 < 0.0f)
            seconds = std::min(seconds, -5.0f * x0 / v0);
    }

    float SmoothStep(float x)
    {
        x = std::clamp(x, 0.0f, 1.0f);
        return x * x * (3.0f - 2.0f * x);
    }

    // The crossfade's incoming share `elapsed` seconds in.
    float FadeShare(const AnimLayerPose& layer, float elapsed)
    {
        const float in = layer.FadeInSeconds > 0.0f ? SmoothStep(elapsed / layer.FadeInSeconds) : 1.0f;
        const float out = layer.FadeOutSeconds > 0.0f ? 1.0f - SmoothStep(elapsed / layer.FadeOutSeconds) : 0.0f;
        return in + out > 0.0f ? in / (in + out) : 1.0f;
    }

    double Elapsed(AnimTick from, AnimTick to, double tickSeconds)
    {
        return (static_cast<double>(to) - static_cast<double>(from)) * tickSeconds;
    }

    float BlendspaceRate(const AnimBoundRig& rig, const AnimBoundBlendspace& space, const float* coordinates)
    {
        std::array<float, kAnimBlendspaceMaxSamples> weights{};
        AnimBlendspaceWeights(space, { coordinates[0], coordinates[1] }, weights);
        const float duration = AnimBlendspaceDuration(rig, space, weights);
        return duration > 0.0f ? 1.0f / duration : 0.0f;
    }

    // What a layer showed at tick `at` under its state before this tick's
    // change: its playback, faded against an outgoing one, with any offset
    // still decaying.
    void Shown(const AnimPoseSources& sources, const AnimLayerPose& layer, std::span<const AnimJointOffset> offsets,
               AnimTick at, double tickSeconds, AnimPoseScratch& scratch, std::vector<Transform3f>& out)
    {
        SampleAnimPlayback(sources, layer.Playing, at, tickSeconds, false, scratch, out);
        if (layer.Fading)
        {
            SampleAnimPlayback(sources, layer.FadingOut, at, tickSeconds, false, scratch, scratch.Fading);
            const float share =
                FadeShare(layer, static_cast<float>(Elapsed(layer.FadeStartTick, at, tickSeconds)));
            for (std::size_t j = 0; j < out.size(); ++j)
                out[j] = Transform3f::Interpolate(scratch.Fading[j], out[j], share);
        }
        if (layer.Offsetting)
        {
            const float elapsed = static_cast<float>(Elapsed(layer.OffsetStartTick, at, tickSeconds));
            for (std::size_t j = 0; j < out.size() && j < offsets.size(); ++j)
                AnimApplyJointOffset(offsets[j], elapsed, out[j]);
        }
    }

    bool SamePlayback(const AnimBoundRig& rig, const AnimLayerPlayback& a, const AnimLayerPlayback& b)
    {
        if (a.Behavior != b.Behavior || a.Content != b.Content || a.ClipStartTick != b.ClipStartTick)
            return false;
        // A blendspace's clip is only its heaviest sample; which one that is
        // changes nothing about the pose.
        const bool mix = a.Content < rig.Contents.size() && rig.Contents[a.Content].Blendspace >= 0;
        return mix || a.Clip == b.Clip;
    }
}

AnimLayerPlayback AnimPlaybackOf(const AnimBoundRig& rig, const AnimLayerContent& layer, AnimTick now)
{
    AnimLayerPlayback playback;
    playback.Behavior = layer.Behavior;
    playback.Content = layer.Content;
    playback.Clip = layer.Clip;
    playback.ClipStartTick = layer.ClipStartTick;
    playback.ClipOffsetSeconds = layer.ClipOffsetSeconds;
    playback.ClipRate = layer.ClipRate;
    playback.Phase = layer.Phase;
    playback.PhaseTick = now;
    playback.Coordinates[0] = layer.Coordinates[0];
    playback.Coordinates[1] = layer.Coordinates[1];
    const AnimBoundBehavior* behavior = rig.FindBehavior(layer.Behavior);
    const bool flow = layer.Content < rig.Contents.size() && rig.Contents[layer.Content].Flow >= 0;
    // A flow section plays its clip once; its loops are new clip starts.
    playback.Cyclic = !flow && (behavior == nullptr || behavior->Policy.Kind == AnimBehaviorKind::Cyclic);
    if (layer.Content < rig.Contents.size() && rig.Contents[layer.Content].Blendspace >= 0)
        playback.PhaseRate = BlendspaceRate(
            rig, rig.Blendspaces[static_cast<std::size_t>(rig.Contents[layer.Content].Blendspace)],
            layer.Coordinates) * layer.ClipRate;
    return playback;
}

void SampleAnimPlayback(const AnimPoseSources& sources, const AnimLayerPlayback& playback, AnimTick at,
                        double tickSeconds, bool reference, AnimPoseScratch& scratch, std::vector<Transform3f>& out)
{
    const AnimBoundRig& rig = *sources.Rig;
    const SkeletonData& skeleton = *sources.Skeleton;
    const auto clipOf = [&](int content) -> const AnimationClipData* {
        if (content < 0 || static_cast<std::size_t>(content) >= rig.Contents.size() || sources.Clips == nullptr)
            return nullptr;
        return sources.Clips->Get(rig.Contents[static_cast<std::size_t>(content)].Clip);
    };

    const int space = playback.Content < rig.Contents.size() ? rig.Contents[playback.Content].Blendspace : -1;
    if (space >= 0)
    {
        const AnimBoundBlendspace& mix = rig.Blendspaces[static_cast<std::size_t>(space)];
        double phase = reference ? 0.0
                                 : static_cast<double>(playback.Phase)
                + Elapsed(playback.PhaseTick, at, tickSeconds) * static_cast<double>(playback.PhaseRate);
        phase = playback.Cyclic ? phase - std::floor(phase) : std::clamp(phase, 0.0, 1.0);
        std::array<float, kAnimBlendspaceMaxSamples> weights{};
        AnimBlendspaceWeights(mix, { playback.Coordinates[0], playback.Coordinates[1] }, weights);

        out.assign(skeleton.Joints.size(), Transform3f{ Vec3d{}, Quatf{ 0.0f, 0.0f, 0.0f, 0.0f }, Vec3d{} });
        float total = 0.0f;
        for (std::size_t s = 0; s < mix.Samples.size(); ++s)
        {
            const AnimationClipData* clip = clipOf(mix.Samples[s].Content);
            if (weights[s] <= 0.0f || clip == nullptr)
                continue;
            SampleAnimationClip(*clip, skeleton, static_cast<float>(phase * clip->DurationSeconds), scratch.Mix);
            for (std::size_t j = 0; j < out.size(); ++j)
            {
                Quatf rotation = scratch.Mix[j].Rotation;
                // Summed rotations must share a hemisphere, or opposite signs
                // of one rotation cancel.
                if (total > 0.0f && out[j].Rotation.Dot(rotation) < 0.0f)
                    rotation = -rotation;
                out[j].Position += scratch.Mix[j].Position * weights[s];
                out[j].Scale += scratch.Mix[j].Scale * weights[s];
                const float w = weights[s];
                out[j].Rotation = Quatf{ out[j].Rotation.X + rotation.X * w, out[j].Rotation.Y + rotation.Y * w,
                                         out[j].Rotation.Z + rotation.Z * w, out[j].Rotation.W + rotation.W * w };
            }
            total += weights[s];
        }
        if (total <= 0.0f)
        {
            AnimBindPose(skeleton, out);
            return;
        }
        for (Transform3f& joint : out)
        {
            joint.Position = joint.Position / total;
            joint.Scale = joint.Scale / total;
            joint.Rotation = joint.Rotation.Normalized();
        }
        return;
    }

    const AnimationClipData* clip = clipOf(playback.Clip == kAnimNoContent ? -1 : static_cast<int>(playback.Clip));
    if (clip == nullptr)
    {
        AnimBindPose(skeleton, out);
        return;
    }
    double time = reference ? 0.0
                            : static_cast<double>(playback.ClipOffsetSeconds)
            + std::max(0.0, Elapsed(playback.ClipStartTick, at, tickSeconds)) * static_cast<double>(playback.ClipRate);
    const double duration = static_cast<double>(clip->DurationSeconds);
    if (duration > 0.0)
    {
        if (playback.Cyclic)
        {
            time = std::fmod(time, duration);
            time = time < 0.0 ? time + duration : time;
        }
        else
        {
            time = std::clamp(time, 0.0, duration);
        }
    }
    SampleAnimationClip(*clip, skeleton, static_cast<float>(time), out);
}

AnimJointOffset AnimInertializeJoint(const Transform3f& shown, const Transform3f& shownBefore,
                                     const Transform3f& target, float seconds, double tickSeconds)
{
    AnimJointOffset offset;
    const float dt = static_cast<float>(tickSeconds);

    const Vec3d difference = shown.Position - target.Position;
    offset.Distance = difference.Magnitude();
    if (offset.Distance > kRestDistance)
    {
        offset.Direction = difference / offset.Distance;
        offset.Speed = dt > 0.0f ? (shown.Position - shownBefore.Position).Dot(offset.Direction) / dt : 0.0f;
        offset.Seconds = seconds;
        Settle(offset.Distance, offset.Speed, offset.Seconds);
    }
    else
    {
        offset.Distance = 0.0f;
    }

    AxisAngle(shown.Rotation * target.Rotation.Inverse(), offset.Axis, offset.Angle);
    if (offset.Angle > kRestAngle)
    {
        Vec3d spinAxis;
        float spin = 0.0f;
        AxisAngle(shown.Rotation * shownBefore.Rotation.Inverse(), spinAxis, spin);
        offset.AngularSpeed = dt > 0.0f ? spin / dt * spinAxis.Dot(offset.Axis) : 0.0f;
        offset.AngleSeconds = seconds;
        Settle(offset.Angle, offset.AngularSpeed, offset.AngleSeconds);
    }
    else
    {
        offset.Angle = 0.0f;
    }
    return offset;
}

void AnimApplyJointOffset(const AnimJointOffset& offset, float elapsed, Transform3f& pose)
{
    if (offset.Distance > 0.0f)
        pose.Position += offset.Direction * Quintic(offset.Distance, offset.Speed, offset.Seconds, elapsed);
    if (offset.Angle > 0.0f)
    {
        const float angle = Quintic(offset.Angle, offset.AngularSpeed, offset.AngleSeconds, elapsed);
        if (angle != 0.0f)
            pose.Rotation = (Quatf::FromAxisAngle(offset.Axis, angle) * pose.Rotation).Normalized();
    }
}

float AnimOffsetRemaining(std::span<const AnimJointOffset> offsets, float elapsed)
{
    float remaining = 0.0f;
    for (const AnimJointOffset& offset : offsets)
        remaining = std::max(remaining, std::abs(Quintic(offset.Distance, offset.Speed, offset.Seconds, elapsed)));
    return remaining;
}

void EvaluateAnimPose(const AnimPoseInput& input, AnimPoseScratch& scratch)
{
    const AnimBoundRig& rig = *input.Sources.Rig;
    AnimPoseState& state = *input.State;
    AnimPosePool::Slot& slot = *input.Slot;
    const AnimTick now = input.Now;
    const double dt = input.TickSeconds;

    // Indices from another binding name other content: start the layers over.
    if (state.BindingGeneration != rig.Generation)
    {
        for (AnimLayerPose& layer : state.Layers)
            layer = AnimLayerPose{};
        state.BindingGeneration = rig.Generation;
    }

    const std::size_t layerCount = std::min<std::size_t>(rig.Layers.size(), slot.Layers);
    std::array<AnimPoseLayer, kAnimMaxLayers> compose{};
    for (std::size_t l = 0; l < layerCount; ++l)
    {
        AnimLayerPose& layer = state.Layers[l];
        const AnimLayerContent& content = input.Content->Layers[l];
        const std::span<AnimJointOffset> offsets = slot.LayerOffsets(l);
        const AnimLayerPlayback incoming = AnimPlaybackOf(rig, content, now);

        if (layer.Posed && !SamePlayback(rig, layer.Playing, incoming))
        {
            const AnimBoundBlendOverride* overridden = nullptr;
            const AnimBlendPolicy policy = rig.ResolveBlend(layer.Playing.Behavior, incoming.Behavior, &overridden);
            const float in = policy.InMs / 1000.0f;
            const AnimBlendMode mode = in > 0.0f ? policy.In : AnimBlendMode::Snap;
            float magnitude = 0.0f;
            if (mode == AnimBlendMode::Inertialize)
            {
                // What was shown, now and a tick ago, against what now plays.
                Shown(input.Sources, layer, offsets, now, dt, scratch, scratch.Shown);
                Shown(input.Sources, layer, offsets, now - 1, dt, scratch, scratch.ShownBefore);
                SampleAnimPlayback(input.Sources, incoming, now, dt, false, scratch, scratch.Incoming);
                float longest = 0.0f;
                for (std::size_t j = 0; j < offsets.size(); ++j)
                {
                    offsets[j] =
                        AnimInertializeJoint(scratch.Shown[j], scratch.ShownBefore[j], scratch.Incoming[j], in, dt);
                    magnitude = std::max(magnitude, offsets[j].Distance);
                    longest = std::max({ longest, offsets[j].Seconds, offsets[j].AngleSeconds });
                }
                layer.Offsetting = longest > 0.0f;
                layer.OffsetStartTick = now;
                layer.OffsetSeconds = longest;
                layer.Fading = false;
            }
            else if (mode == AnimBlendMode::Crossfade)
            {
                layer.FadingOut = layer.Playing;
                layer.FadeStartTick = now;
                layer.FadeInSeconds = in;
                layer.FadeOutSeconds = policy.OutMs > 0.0f ? policy.OutMs / 1000.0f : in;
                layer.Fading = true;
            }
            else
            {
                layer.Fading = false;
                layer.Offsetting = false;
            }

            if (input.Log != nullptr)
            {
                AnimDecisionRecord record;
                record.Tick = now;
                record.Cause = AnimDecisionCause::BlendApplied;
                record.Layer = static_cast<std::uint8_t>(l);
                record.Behavior = incoming.Behavior;
                record.PreviousBehavior = layer.Playing.Behavior;
                record.Content = incoming.Content;
                record.Blend = mode;
                record.BlendSeconds = mode == AnimBlendMode::Snap ? 0.0f : in;
                record.BlendMagnitude = magnitude;
                record.BlendOverridden = overridden != nullptr;
                input.Log->Append(record);
            }
        }
        layer.Playing = incoming;
        layer.Posed = true;

        // Spent blends stop costing a second sample and the offset pass.
        if (layer.Fading
            && Elapsed(layer.FadeStartTick, now, dt) >= std::max(layer.FadeInSeconds, layer.FadeOutSeconds))
            layer.Fading = false;
        if (layer.Offsetting && Elapsed(layer.OffsetStartTick, now, dt) >= layer.OffsetSeconds)
            layer.Offsetting = false;

        std::vector<Transform3f>& shown = scratch.Shown;
        Shown(input.Sources, layer, offsets, now, dt, scratch, shown);
        const std::span<Transform3f> pose = slot.LayerPose(l);
        std::copy_n(shown.begin(), std::min(shown.size(), pose.size()), pose.begin());

        // A layer with nothing to play and nothing fading adds nothing.
        const bool plays = content.Content != kAnimNoContent || layer.Fading;
        const AnimBoundLayer& bound = rig.Layers[l];
        compose[l].Pose = plays ? std::span<const Transform3f>(pose) : std::span<const Transform3f>();
        compose[l].Weight = AnimLayerWeight(rig, l, input.Selection);
        compose[l].Mode = bound.Mode;
        compose[l].Mask = bound.Mask;
        if (plays && bound.Mode == AnimLayerMode::Additive)
        {
            SampleAnimPlayback(input.Sources, layer.Playing, now, dt, true, scratch, scratch.References[l]);
            compose[l].Reference = scratch.References[l];
        }
    }

    ComposeAnimPose(*input.Sources.Skeleton, std::span(compose.data(), layerCount), scratch.Composed);
    std::swap(slot.Previous, slot.Current);
    slot.HasPrevious = slot.HasCurrent && slot.Tick + 1 == now;
    std::copy_n(scratch.Composed.begin(), std::min(scratch.Composed.size(), slot.Current.size()), slot.Current.begin());
    slot.HasCurrent = true;
    slot.Tick = now;
}
