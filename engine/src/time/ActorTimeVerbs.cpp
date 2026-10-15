#include <time/ActorTimeVerbs.h>

#include <authored/AuthoredValue.h>
#include <ecs/Query.h>
#include <ecs/World.h>
#include <time/ActorTime.h>
#include <time/ActorTimeLeases.h>
#include <time/SimClock.h>

#include <cmath>
#include <string>
#include <utility>

void DeclareActorTimeVerbs(VerbRegistrationScope& scope)
{
    VerbDefinition scale;
    scale.Name = std::string(kTimeScaleLease);
    scale.DisplayName = "Scale time";
    scale.Description = "Runs an actor's time faster or slower while held: its movement, effects, phases and animation.";
    scale.Category = "Time";
    scale.Kind = VerbKind::Lease;
    scale.Prediction = AuthoredPrediction::Simulation;
    DataFieldSchema target = MakeDataField(DataFieldKind::Entity, "target", "Target", "The actor whose time runs differently.");
    target.Reference.ComponentIdentity = std::string(ComponentDefinition<ActorTime>::Identity);
    DataFieldSchema factor = MakeDataField(DataFieldKind::Float, "scale", "Scale", "One is unchanged; a half is half speed.");
    factor.Numeric.Minimum = 0.01;
    factor.Numeric.Maximum = static_cast<double>(kActorScaleHardMaximumQ16) / static_cast<double>(kActorTimeOne);
    scale.Arguments.Children = { std::move(target), std::move(factor) };
    (void)scope.Declare(std::move(scale));
}

VerbAdmission ActorTimeOperations::Scale(const VerbInvocation& invocation)
{
    const std::uint64_t tick = invocation.Tick != 0 ? invocation.Tick : Clock->GetTickIndex();
    if (invocation.Step == LeaseStep::Release)
    {
        if (!Entities->IsRegistered<ActorTimeLeases>())
            return VerbAdmission::Accepted;
        Query<Write<ActorTimeLeases>> held(*Entities);
        held.ForEachChunk([&](auto& view) {
            auto leases = view.template Write<ActorTimeLeases>();
            for (std::uint32_t i = 0; i < view.Count(); ++i)
                ReleaseActorTimeLeases(leases[i], invocation.Owner, tick);
        });
        return VerbAdmission::Accepted;
    }

    EntityId target;
    double scale = 1.0;
    if (invocation.Arguments == nullptr || !invocation.Arguments->TryGetEntity(0, target)
        || !invocation.Arguments->TryGetFloat(1, scale) || !Entities->IsAlive(target))
        return VerbAdmission::InvalidArguments;
    if (!Entities->IsRegistered<ActorTime>() || !Entities->HasComponent<ActorTime>(target)
        || !Entities->IsRegistered<ActorTimeLeases>())
        return VerbAdmission::Refused;
    if (!Entities->HasComponent<ActorTimeLeases>(target))
        Entities->AddComponent<ActorTimeLeases>(target, ActorTimeLeases{});
    const std::int64_t scaleQ16 = std::llround(scale * static_cast<double>(kActorTimeOne));
    return AcquireActorTimeLease(*Entities->TryGet<ActorTimeLeases>(target), invocation.Owner, scaleQ16, tick)
        ? VerbAdmission::Accepted
        : VerbAdmission::Refused;
}
