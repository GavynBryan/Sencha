#include <effects/EffectVerbs.h>

#include <authored/AuthoredValueTraits.h>
#include <attributes/AttributeSet.h>
#include <authored/AuthoredValue.h>
#include <ecs/World.h>
#include <effects/EffectDefinitionData.h>
#include <effects/EffectRegistry.h>
#include <effects/EffectSystem.h>
#include <gameplay_tags/GameplayTagQueryNames.h>
#include <gameplay_tags/GameplayTagRegistry.h>
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
        DataFieldSchema instigator = MakeDataField(DataFieldKind::Entity, "instigator", "Instigator",
                                                   "Who applied it, for whatever reacts to it.");
        instigator.Required = false;
        DataFieldSchema from = MakeDataField(DataFieldKind::Vector, "from", "From",
                                             "Where it came from, for whatever reacts to it. The instigator when "
                                             "absent.");
        from.Required = false;
        from.VectorLength = 3;
        DataFieldSchema arguments = EmptyVerbArguments();
        arguments.Children = { std::move(target), std::move(effect), std::move(scale), std::move(instigator),
                               std::move(from) };
        return arguments;
    }

    DataFieldSchema ApplyArguments()
    {
        DataFieldSchema arguments = EffectArguments();
        DataFieldSchema keep = MakeDataField(DataFieldKind::Bool, "keep", "Keep",
                                             "Whether a save keeps it while it lasts. When absent, it is kept, as "
                                             "anything nothing owns is.");
        keep.Required = false;
        arguments.Children.push_back(std::move(keep));
        return arguments;
    }

    DataFieldSchema RemoveArguments()
    {
        DataFieldSchema target = MakeDataField(DataFieldKind::Entity, "target", "Target",
                                               "The entity whose lasting effects end.");
        DataFieldSchema effect = MakeDataField(DataFieldKind::DataAssetRef, "effect", "Effect",
                                               "Ends every instance of this effect. Name it or kinds, not both.");
        effect.Reference.DataSubtype = std::string(kEffectDefinitionType);
        effect.Required = false;
        const auto names = [](std::string key, std::string label) {
            DataFieldSchema list = MakeDataField(DataFieldKind::Array, std::move(key), std::move(label));
            list.Required = false;
            list.Children.push_back(MakeDataField(DataFieldKind::String, "tag", "Tag"));
            return list;
        };
        DataFieldSchema kinds = MakeDataField(DataFieldKind::Record, "kinds", "Kinds",
                                              "Ends every effect whose kinds this query matches, each tag by name "
                                              "and beneath it. Name it or an effect, not both.");
        kinds.Required = false;
        kinds.Children = { names("all", "All of"), names("any", "Any of"), names("none", "None of") };
        DataFieldSchema arguments = EmptyVerbArguments();
        arguments.Children = { std::move(target), std::move(effect), std::move(kinds) };
        return arguments;
    }

    std::vector<std::string> NamesIn(const AuthoredValue& list)
    {
        std::vector<std::string> names;
        for (const AuthoredValue& element : list.Children())
            if (std::string_view name; element.TryGetString(name))
                names.emplace_back(name);
        return names;
    }

    struct EffectRequest
    {
        EntityId Target;
        EffectId Effect;
        float Scale = 1.0f;
        EffectSource Source;
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
        (void)invocation.Arguments->TryGetEntity(3, out.Source.Instigator);
        out.Source.HasPoint = ReadAuthoredArgument(*invocation.Arguments, 4, out.Source.Point);

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
    apply.Arguments = ApplyArguments();
    (void)scope.Declare(std::move(apply));

    VerbDefinition remove;
    remove.Name = std::string(kEffectRemoveVerb);
    remove.DisplayName = "Remove effects";
    remove.Description = "Ends an entity's lasting effects of one definition, or of the kinds a tag query matches.";
    remove.Category = "Effects";
    remove.Arguments = RemoveArguments();
    remove.Refusals = VerbRefusalsOf<EffectRemoveRefusal>();
    remove.Result = MakeDataField(DataFieldKind::Int, "count", "Count", "How many lasting effects it ended.");
    (void)scope.Declare(std::move(remove));

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
    return Clock->TimeOfTick(invocation.Tick);
}

VerbAdmission EffectOperations::Apply(const VerbInvocation& invocation)
{
    EffectRequest request;
    if (const VerbAdmission read = ReadRequest(*Entities, invocation, request); read != VerbAdmission::Accepted)
        return read;
    bool keep = true;
    const EffectKeeping keeping = !invocation.Arguments->TryGetBool(5, keep) ? EffectKeeping::ByOwner
                                  : keep                                    ? EffectKeeping::Kept
                                                                            : EffectKeeping::NotKept;
    (void)ApplyEffect(*Entities, request.Target, request.Effect, Now(invocation), {}, request.Scale, request.Source,
                      keeping);
    return VerbAdmission::Accepted;
}

VerbAnswer EffectOperations::Remove(const VerbInvocation& invocation)
{
    EntityId target;
    if (invocation.Arguments == nullptr || !invocation.Arguments->TryGetEntity(0, target))
        return VerbArgumentAnswer(VerbAdmission::InvalidArguments, 0);
    const AssetRef* effect = nullptr;
    const bool namesEffect = invocation.Arguments->TryGetDataAsset(1, effect);
    const AuthoredValue& query = invocation.Arguments->At(2);
    const bool namesKinds = query.Kind() == AuthoredValueKind::Record;
    if (namesEffect == namesKinds)
        return VerbAnswerOf(namesEffect ? EffectRemoveRefusal::BothNamed : EffectRemoveRefusal::NothingNamed);
    if (!Entities->IsAlive(target))
        return VerbArgumentAnswer(VerbAdmission::InvalidArguments, 0);

    std::size_t ended = 0;
    if (namesEffect)
    {
        const EffectRegistry* effects = Entities->TryGetResource<EffectRegistry>();
        const EffectId definition = effects != nullptr ? effects->Find(effect->Path) : EffectId{};
        if (!definition.IsValid())
            return VerbArgumentAnswer(VerbAdmission::InvalidArguments, 1);
        ended = RemoveEffectsOn(*Entities, target, definition);
    }
    else
    {
        const GameplayTagRegistry* tags = Entities->TryGetResource<GameplayTagRegistry>();
        const std::span<const AuthoredValue> lists = query.Children();
        const auto namesAt = [&](std::size_t slot) {
            return slot < lists.size() ? NamesIn(lists[slot]) : std::vector<std::string>{};
        };
        const GameplayTagQueryNames names{ .All = namesAt(0), .Any = namesAt(1), .None = namesAt(2) };
        std::vector<std::string> missing;
        const GameplayTagQuery kinds = tags != nullptr ? ResolveTagQuery(names, *tags, missing) : GameplayTagQuery{};
        if (tags == nullptr || !missing.empty() || names.Empty())
            return VerbArgumentAnswer(VerbAdmission::InvalidArguments, 2);
        ended = RemoveEffectsOn(*Entities, target, kinds);
    }
    if (invocation.Result != nullptr)
        *invocation.Result = AuthoredValue::Int(static_cast<std::int64_t>(ended));
    return VerbAnswer{};
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
    return ApplyEffect(*Entities, request.Target, request.Effect, Now(invocation), invocation.Owner, request.Scale,
                       request.Source)
               .IsValid()
        ? VerbAdmission::Accepted
        : VerbAdmission::Refused;
}
