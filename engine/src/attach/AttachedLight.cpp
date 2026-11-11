#include <attach/AttachedLight.h>

#include <ecs/World.h>
#include <presentation/HeldEffects.h>

#include <algorithm>
#include <cmath>

AttachResult AttachedLightState::Hold(World& world, AttachedLightSpec spec, std::uint64_t tick, const LeaseOwner& holder)
{
    if (Settings.ReducedFlashing)
    {
        spec.PeakScale = 1.0f;
        spec.Envelope.Attack = std::max(spec.Envelope.Attack, kReducedFlashingRampSeconds);
        spec.Envelope.Release = std::max(spec.Envelope.Release, kReducedFlashingRampSeconds);
    }
    const float steady = spec.Attachment.Light.Intensity;
    spec.Attachment.Light.Intensity = 0.0f;

    const auto held = std::ranges::find_if(
        Lights, [&](const Held& light) { return light.Timing.Holder.IsSet() && light.Timing.Holder == holder; });
    if (held != Lights.end())
    {
        PointLightComponent* light = world.IsAlive(held->Entity) ? world.TryGet<PointLightComponent>(held->Entity) : nullptr;
        if (light == nullptr)
            return AttachResult::NoRoot;
        light->Color = spec.Attachment.Light.Color;
        light->Range = spec.Attachment.Light.Range;
        light->PacksFirst = spec.Attachment.Light.PacksFirst;
        held->Timing.Envelope = spec.Envelope;
        held->Intensity = steady;
        held->PeakScale = spec.PeakScale;
        held->PulseSeconds = spec.PulseSeconds;
        return AttachResult::Updated;
    }

    AttachResult result = AttachResult::NoRoot;
    const EntityId entity = CreateLightAttachment(world, spec.Attachment, &result);
    if (entity.IsValid())
        Lights.push_back({ .Entity = entity,
                           .Timing = CameraEffectTiming::Held(spec.Envelope, tick, {}, holder),
                           .Intensity = steady,
                           .PeakScale = spec.PeakScale,
                           .PulseSeconds = spec.PulseSeconds });
    return result;
}

void AttachedLightState::Release(const LeaseOwner& holder, std::uint64_t tick, double tickSeconds)
{
    ReleaseHeldEffects(Lights, holder, tick, tickSeconds);
}

void AttachedLightState::Update(World& world, const CameraEffectClock& now)
{
    std::erase_if(Lights, [&](const Held& light) {
        PointLightComponent* shown = world.IsAlive(light.Entity) ? world.TryGet<PointLightComponent>(light.Entity) : nullptr;
        if (shown == nullptr)
            return true;
        const double elapsed = light.Timing.Elapsed(now);
        if (light.Timing.Expired(elapsed))
        {
            DestroyAttachment(world, light.Entity);
            return true;
        }
        const double pulse = light.PulseSeconds > 0.0f
                                 ? (light.PeakScale - 1.0) * std::exp(-elapsed / light.PulseSeconds)
                                 : 0.0;
        shown->Intensity = static_cast<float>(light.Intensity * light.Timing.Level(elapsed) * (1.0 + pulse));
        return false;
    });
}

void PresentAttachedLights(World& world, const FixedSimulationLoop& clock, double presentationAlpha)
{
    if (AttachedLightState* state = world.TryGetResource<AttachedLightState>())
        state->Update(world, CameraEffectClock::At(clock, presentationAlpha));
}
