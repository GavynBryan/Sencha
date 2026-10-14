#include <effects/EffectVerbs.h>

#include <attributes/AttributeSet.h>
#include <authored/AuthoredValue.h>
#include <ecs/World.h>
#include <effects/EffectDefinitionData.h>
#include <effects/EffectRegistry.h>
#include <effects/EffectSystem.h>
#include <time/SimClock.h>

#include <string>
#include <utility>

namespace
{
    DataFieldSchema EffectArguments()
    {
        DataFieldSchema target = MakeDataField(DataFieldKind::Entity, "target", "Target",
                                               "The entity whose attributes the effect modifies.");
        target.Reference.ComponentIdentity = std::string(ComponentDefinition<AttributeSet>::Identity);
        DataFieldSchema effect = MakeDataField(DataFieldKind::DataAssetRef, "effect", "Effect");
        effect.Reference.DataSubtype = std::string(kEffectDefinitionType);
        DataFieldSchema scale = MakeDataField(DataFieldKind::Float, "scale", "Scale",
                                              "Multiplies every modifier's magnitude.");
        scale.Default = 1.0;
        DataFieldSchema arguments = EmptyVerbArguments();
        arguments.Children = { std::move(target), std::move(effect), std::move(scale) };
        return arguments;
    }

    struct EffectRequest
    {
        EntityId Target;
        EffectId Effect;
        float Scale = 1.0f;
    };

    // Reads and checks the arguments both operations share.
    VerbAdmission ReadRequest(const World& world, const VerbInvocation& invocation, EffectRequest& out)
    {
        const AssetRef* effect = nullptr;
        double scale = 1.0;
        if (invocation.Arguments == nullptr || !invocation.Arguments->TryGetEntity(0, out.Target)
            || !invocation.Arguments->TryGetDataAsset(1, effect) || !invocation.Arguments->TryGetFloat(2, scale))
            return VerbAdmission::InvalidArguments;
        out.Scale = static_cast<float>(scale);

        const EffectRegistry* effects = world.TryGetResource<EffectRegistry>();
        out.Effect = effects != nullptr ? effects->Find(effect->Path) : EffectId{};
        const EffectDefinition* definition = effects != nullptr ? effects->Get(out.Effect) : nullptr;
        if (definition == nullptr || !world.IsAlive(out.Target))
            return VerbAdmission::InvalidArguments;

        const AttributeSet* attributes =
            world.IsRegistered<AttributeSet>() ? world.TryGet<AttributeSet>(out.Target) : nullptr;
        if (attributes == nullptr)
            return VerbAdmission::Refused;
        for (const EffectModifier& modifier : definition->Modifiers)
            if (!attributes->Has(modifier.Attr))
                return VerbAdmission::Refused;
        return VerbAdmission::Accepted;
    }
}

void DeclareEffectVerbs(VerbRegistrationScope& scope)
{
    VerbDefinition apply;
    apply.Name = std::string(kEffectApplyVerb);
    apply.DisplayName = "Apply effect";
    apply.Description = "Applies an effect to an entity's attributes.";
    apply.Category = "Effects";
    apply.Arguments = EffectArguments();
    (void)scope.Declare(std::move(apply));

    VerbDefinition hold;
    hold.Name = std::string(kEffectHoldLease);
    hold.DisplayName = "Hold effect";
    hold.Description = "Keeps a lasting effect on an entity until its holder lets go.";
    hold.Category = "Effects";
    hold.Kind = VerbKind::Lease;
    hold.Arguments = EffectArguments();
    (void)scope.Declare(std::move(hold));
}

FixedSimTime EffectOperations::Now(const VerbInvocation& invocation) const
{
    return { .DeltaSeconds = Clock->GetFixedDt(),
             .TickIndex = invocation.Tick != 0 ? invocation.Tick : Clock->GetTickIndex() };
}

VerbAdmission EffectOperations::Apply(const VerbInvocation& invocation)
{
    EffectRequest request;
    if (const VerbAdmission read = ReadRequest(*Entities, invocation, request); read != VerbAdmission::Accepted)
        return read;
    (void)ApplyEffect(*Entities, request.Target, request.Effect, Now(invocation), {}, request.Scale);
    return VerbAdmission::Accepted;
}

VerbAdmission EffectOperations::Hold(const VerbInvocation& invocation)
{
    if (invocation.Step == LeaseStep::Release)
    {
        (void)RemoveEffectsOwnedBy(*Entities, invocation.Owner);
        return VerbAdmission::Accepted;
    }
    EffectRequest request;
    if (const VerbAdmission read = ReadRequest(*Entities, invocation, request); read != VerbAdmission::Accepted)
        return read;
    const EffectDefinition* definition = Entities->GetResource<EffectRegistry>().Get(request.Effect);
    if (definition->Duration == EffectDuration::Instant)
        return VerbAdmission::InvalidArguments;
    return ApplyEffect(*Entities, request.Target, request.Effect, Now(invocation), invocation.Owner, request.Scale)
               .IsValid()
        ? VerbAdmission::Accepted
        : VerbAdmission::Refused;
}
