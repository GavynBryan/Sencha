#include <anim/AnimRequestVerbs.h>

#include <anim/AnimRequestJournal.h>
#include <authored/AuthoredValue.h>
#include <ecs/World.h>
#include <time/SimClock.h>

#include <string>

namespace
{
    DataFieldSchema Argument(std::string key, DataFieldKind kind, std::string label, std::string description)
    {
        DataFieldSchema field;
        field.Key = std::move(key);
        field.Kind = kind;
        field.DisplayName = std::move(label);
        field.Description = std::move(description);
        return field;
    }

    // Who asked: the participant behind the invocation, else whatever produced it.
    EntityId SourceOf(const VerbInvocation& invocation, EntityId target)
    {
        if (invocation.Instigator.IsValid())
            return invocation.Instigator;
        return invocation.Producer.IsValid() ? invocation.Producer : target;
    }
}

void DeclareAnimRequestVerbs(VerbRegistrationScope& scope)
{
    VerbDefinition request;
    request.Name = std::string(kAnimRequestVerb);
    request.DisplayName = "Request animation";
    request.Description = "Asks an animated entity to present an intent its rig declares.";
    request.Category = "Animation";
    DataFieldSchema lifetime = Argument("lifetime", DataFieldKind::Enum, "Lifetime",
                                        "Held lasts until anim.cancel; fixed for `ticks`; impulse for one tick.");
    lifetime.EnumChoices = { { "held", "Held", {} }, { "fixed", "Fixed", {} }, { "impulse", "Impulse", {} } };
    lifetime.Default = std::string("held");
    DataFieldSchema ticks = Argument("ticks", DataFieldKind::Int, "Ticks", "How long a fixed request lasts.");
    ticks.Default = std::int64_t{ 0 };
    request.Arguments.Children = {
        Argument("target", DataFieldKind::Entity, "Target", "The animated entity."),
        Argument("intent", DataFieldKind::GameplayTag, "Intent", "What to present, as its rig names it."),
        std::move(lifetime),
        std::move(ticks),
    };
    (void)scope.Declare(std::move(request));

    VerbDefinition cancel;
    cancel.Name = std::string(kAnimCancelVerb);
    cancel.DisplayName = "End animation request";
    cancel.Description = "Ends the held request this producer asked the entity for.";
    cancel.Category = "Animation";
    cancel.Arguments.Children = {
        Argument("target", DataFieldKind::Entity, "Target", "The animated entity."),
        Argument("intent", DataFieldKind::GameplayTag, "Intent", "The intent it was asked for."),
    };
    (void)scope.Declare(std::move(cancel));
}

VerbAdmission AnimRequestOperations::Request(const VerbInvocation& invocation)
{
    EntityId target;
    AnimRequestDesc desc;
    std::string_view lifetime;
    std::int64_t ticks = 0;
    if (invocation.Arguments == nullptr || !invocation.Arguments->TryGetEntity(0, target)
        || !invocation.Arguments->TryGetTag(1, desc.Intent) || !invocation.Arguments->TryGetEnum(2, lifetime)
        || !invocation.Arguments->TryGetInt(3, ticks) || !Entities->IsAlive(target))
        return VerbAdmission::InvalidArguments;
    desc.Lifetime = lifetime == "fixed" ? AnimRequestLifetime::Fixed
                  : lifetime == "impulse" ? AnimRequestLifetime::Impulse
                                          : AnimRequestLifetime::Held;
    desc.FixedTicks = static_cast<std::uint32_t>(std::max<std::int64_t>(ticks, 0));
    desc.Source = SourceOf(invocation, target);
    desc.Cause = invocation.Id;
    const std::uint64_t tick = invocation.Tick != 0 ? invocation.Tick : Clock->GetTickIndex();
    return RequestAnimation(*Entities, target, desc, tick).Accepted() ? VerbAdmission::Accepted
                                                                      : VerbAdmission::Refused;
}

VerbAdmission AnimRequestOperations::Cancel(const VerbInvocation& invocation)
{
    EntityId target;
    GameplayTagId intent;
    if (invocation.Arguments == nullptr || !invocation.Arguments->TryGetEntity(0, target)
        || !invocation.Arguments->TryGetTag(1, intent) || !Entities->IsAlive(target)
        || !Entities->IsRegistered<AnimRequestSet>())
        return VerbAdmission::InvalidArguments;
    const AnimRequestSet* set = static_cast<const World&>(*Entities).TryGet<AnimRequestSet>(target);
    if (set == nullptr)
        return VerbAdmission::InvalidArguments;
    const EntityId source = SourceOf(invocation, target);
    const std::uint64_t tick = invocation.Tick != 0 ? invocation.Tick : Clock->GetTickIndex();
    for (const AnimRequest& request : set->Records)
    {
        if (!request.Occupied || request.IsCancelled() || request.Id.Source != source || request.Intent != intent)
            continue;
        return CancelAnimation(*Entities, target, request.Id, AnimCancelReason::Released, tick)
            ? VerbAdmission::Accepted
            : VerbAdmission::Refused;
    }
    return VerbAdmission::Refused;
}
