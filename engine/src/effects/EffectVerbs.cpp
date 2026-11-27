#include <effects/EffectVerbs.h>

#include <attributes/AttributeSet.h>
#include <ecs/World.h>
#include <effects/EffectRegistry.h>
#include <effects/EffectSystem.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <time/SimClock.h>

#include <string>
#include <vector>

namespace
{
    VerbAnswer Admit(const World& world, EntityId target, AuthoredDataRef named, EffectId& effect)
    {
        const EffectRegistry* effects = world.TryGetResource<EffectRegistry>();
        effect = effects != nullptr ? effects->Find(named.Reference->Path) : EffectId{};
        const EffectDefinition* definition = effects != nullptr ? effects->Get(effect) : nullptr;
        if (definition == nullptr)
            return VerbArgumentAnswer(VerbAdmission::InvalidArguments, 1);
        const AttributeSet* attributes = world.IsRegistered<AttributeSet>() ? world.TryGet<AttributeSet>(target) : nullptr;
        if (attributes == nullptr)
            return VerbAdmission::Refused;
        for (const EffectModifier& modifier : definition->Modifiers)
            if (!attributes->Has(modifier.Attr))
                return VerbAdmission::Refused;
        return VerbAnswer{};
    }

    EffectSource SourceOf(std::optional<EntityId> instigator, std::optional<Vec3d> from)
    {
        EffectSource source;
        source.Instigator = instigator.value_or(EntityId{});
        source.HasPoint = from.has_value();
        if (from.has_value())
            source.Point = *from;
        return source;
    }
}

FixedSimTime EffectOperations::Now(const VerbInvocation& invocation) const
{
    return Clock->TimeOfTick(invocation.Tick);
}

VerbAnswer EffectOperations::Apply(const VerbInvocation& invocation, EntityId target, AuthoredDataRef effect,
                                   float scale, std::optional<EntityId> instigator, std::optional<Vec3d> from,
                                   std::optional<bool> keep)
{
    EffectId applied;
    if (const VerbAnswer admitted = Admit(*Entities, target, effect, applied); !admitted.Accepted())
        return admitted;
    const EffectKeeping keeping = !keep.has_value() ? EffectKeeping::ByOwner
                                  : *keep           ? EffectKeeping::Kept
                                                    : EffectKeeping::NotKept;
    (void)ApplyEffect(*Entities, target, applied, Now(invocation), {}, scale, SourceOf(instigator, from), keeping);
    return VerbAnswer{};
}

VerbResult<std::int64_t, EffectRemoveRefusal> EffectOperations::Remove(EntityId target,
                                                                       std::optional<AuthoredDataRef> effect,
                                                                       std::optional<GameplayTagQueryNames> kinds)
{
    if (effect.has_value() == kinds.has_value())
        return effect.has_value() ? EffectRemoveRefusal::BothNamed : EffectRemoveRefusal::NothingNamed;
    if (effect.has_value())
    {
        const EffectRegistry* effects = Entities->TryGetResource<EffectRegistry>();
        const EffectId definition = effects != nullptr ? effects->Find(effect->Reference->Path) : EffectId{};
        if (!definition.IsValid())
            return VerbArgumentAnswer(VerbAdmission::InvalidArguments, 1);
        return static_cast<std::int64_t>(RemoveEffectsOn(*Entities, target, definition));
    }
    const GameplayTagRegistry* tags = Entities->TryGetResource<GameplayTagRegistry>();
    std::vector<std::string> missing;
    const GameplayTagQuery query = tags != nullptr ? ResolveTagQuery(*kinds, *tags, missing) : GameplayTagQuery{};
    if (tags == nullptr || !missing.empty() || kinds->Empty())
        return VerbArgumentAnswer(VerbAdmission::InvalidArguments, 2);
    return static_cast<std::int64_t>(RemoveEffectsOn(*Entities, target, query));
}

VerbAnswer EffectOperations::Hold(const VerbInvocation& invocation, EntityId target, AuthoredDataRef effect,
                                  float scale, std::optional<EntityId> instigator, std::optional<Vec3d> from)
{
    EffectId held;
    if (const VerbAnswer admitted = Admit(*Entities, target, effect, held); !admitted.Accepted())
        return admitted;
    if (Entities->GetResource<EffectRegistry>().Get(held)->Duration == EffectDuration::Instant)
        return VerbArgumentAnswer(VerbAdmission::InvalidArguments, 1);
    return ApplyEffect(*Entities, target, held, Now(invocation), invocation.Owner, scale, SourceOf(instigator, from))
               .IsValid()
        ? VerbAnswer{}
        : VerbAnswer(VerbAdmission::Refused);
}

VerbAdmission EffectOperations::EndHold(const VerbInvocation& invocation)
{
    (void)RemoveEffectsOwnedBy(*Entities, invocation.Owner);
    return VerbAdmission::Accepted;
}
