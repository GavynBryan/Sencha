#include <controller/LookIntegrationSystem.h>

#include <controller/LookOrientation.h>
#include <ecs/Query.h>
#include <input/InputActionState.h>

namespace
{

// One pass's look input, split into its two mechanical kinds: displacement
// that is already an amount, and a sampled rate that covers however much time
// the pass's consumer integrates over.
struct LookInput
{
    Vec2d Displacement;
    Vec2d Rate;
};

// The look action's value for one pass, already conditioned by the binding.
// Returns false when nothing can be read, which is not the same as a zero
// input: a pass with no binding must not clear what a pass with one
// accumulated.
bool ReadLookInput(const World& world,
                   const InputActionView& actions,
                   const InputActionView& sampled,
                   LookInput& out)
{
    const LookInputBinding* binding = world.TryGetResource<LookInputBinding>();
    if (binding == nullptr)
        return false;

    // The published value mixes both kinds; the sampled share is published
    // beside it. The difference is the displacement.
    const Vec2d value = actions.Axis2(binding->Look);
    out.Rate = sampled.Axis2(binding->Look);
    out.Displacement = Vec2d(value.X - out.Rate.X, value.Y - out.Rate.Y);
    return true;
}

} // namespace

void LookIntegrationSystem::PreSimulate(PreSimulateContext& ctx)
{
    World& world = ctx.Entities;
    const InputActionState* actions = world.TryGetResource<InputActionState>();
    if (actions == nullptr)
        return;

    LookInput look;
    if (!ReadLookInput(world, actions->Frame(), actions->FrameSampled(), look))
        return;

    PendingLookInput* pending = world.TryGetResource<PendingLookInput>();
    if (pending == nullptr)
        pending = &world.AddResource<PendingLookInput>();

    // Signed here rather than where it is applied, so the lead and the aim it
    // leads agree on which way the input turns the view.
    pending->Yaw -= static_cast<float>(look.Displacement.X);
    pending->Pitch -= static_cast<float>(look.Displacement.Y);

    // The rate is a fact about now, not an accumulation: this frame's sample
    // replaces the last one, and the wall time it covers is derived where the
    // view is placed.
    pending->RateYaw = -static_cast<float>(look.Rate.X);
    pending->RatePitch = -static_cast<float>(look.Rate.Y);
}

void LookIntegrationSystem::FixedLogic(FixedLogicContext& ctx)
{
    World& world = ctx.Entities;
    const InputActionState* actions = world.TryGetResource<InputActionState>();
    if (actions == nullptr)
        return;

    LookInput look;
    if (!ReadLookInput(world, actions->Tick(), actions->TickSampled(), look))
        return;

    // Unconditionally, and before the entities are touched: this tick consumed
    // the frame's share of latched motion whether or not anything was aiming
    // with it, and a displacement lead left standing would be added to the
    // view a second time on top of the aim that has already absorbed it. The
    // rate is untouched -- it is a sample, not something a tick uses up.
    if (PendingLookInput* pending = world.TryGetResource<PendingLookInput>())
    {
        pending->Yaw = 0.0f;
        pending->Pitch = 0.0f;
    }

    if (!world.IsRegistered<LookOrientation>() || !world.IsRegistered<LocalLookControl>())
        return;

    // The tick turns by the displacement that arrived plus the rate over the
    // simulated time this tick covers. A sample multiplied by fixed delta is
    // what makes stick turn speed a property of the stick, not of how many
    // frames the display happened to run.
    const double dt = ctx.Time.DeltaSeconds;
    const float yawStep =
        static_cast<float>(look.Displacement.X + look.Rate.X * dt);
    const float pitchStep =
        static_cast<float>(look.Displacement.Y + look.Rate.Y * dt);
    if (yawStep == 0.0f && pitchStep == 0.0f)
        return;

    Query<Write<LookOrientation>, With<LocalLookControl>> query(world);
    query.ForEachChunkIn(ctx.Partitions, [&](auto& view)
    {
        auto orientations = view.template Write<LookOrientation>();
        for (std::uint32_t index = 0; index < view.Count(); ++index)
        {
            LookOrientation& orientation = orientations[index];
            ApplyLook(orientation, orientation.Yaw - yawStep,
                      orientation.Pitch - pitchStep);
        }
    });
}
