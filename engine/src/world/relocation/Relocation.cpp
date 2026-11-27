#include <world/relocation/Relocation.h>

#include <ecs/PartitionFollowers.h>
#include <ecs/World.h>
#include <math/geometry/3d/RigidMapping.h>
#include <world/relocation/RelocationRules.h>
#include <world/relocation/RelocationSerial.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformHierarchy.h>

#include <algorithm>

namespace
{
    using ComponentRule = RelocationRules::ComponentRule;

    std::span<const ComponentRule> RulesOf(const World& world)
    {
        const RelocationRules* rules = world.TryGetResource<RelocationRules>();
        return rules != nullptr ? rules->ComponentRules() : std::span<const ComponentRule>{};
    }

    std::string_view RefusingName(const World& world, std::span<const ComponentRule> rules,
                                  std::span<const EntityId> group)
    {
        for (const ComponentRule& rule : rules)
            if (rule.Kind == RelocationRuleKind::Refuses)
                for (const EntityId member : group)
                    if (world.HasComponent(member, rule.Component))
                    {
                        const ComponentMeta* meta = world.GetMeta(rule.Component);
                        return meta != nullptr ? meta->Name : std::string_view{ "unnamed component" };
                    }
        return {};
    }

    void MoveToPartition(World& world, std::span<const EntityId> group, StoragePartitionId partition)
    {
        for (const EntityId member : group)
            if (world.GetEntityPartition(member) != partition)
                (void)world.MoveEntityToPartition(member, partition);
        (void)DispatchPartitionMoves(world);
    }

    bool RunsInStage(RelocationRuleKind kind, RelocationRuleKind stage)
    {
        return kind == stage || (stage == RelocationRuleKind::Maps && kind == RelocationRuleKind::MapsWhenUpright);
    }

    // Returns whether a rule was kept back because the mapping tilts world up.
    bool RunComponentRules(World& world, std::span<const ComponentRule> rules, std::span<const EntityId> group,
                           const RelocationFrame& frame, RelocationRuleKind stage)
    {
        bool kept = false;
        for (const EntityId member : group)
            for (const ComponentRule& rule : rules)
            {
                if (!RunsInStage(rule.Kind, stage) || !world.HasComponent(member, rule.Component))
                    continue;
                if (rule.Kind == RelocationRuleKind::MapsWhenUpright && !frame.Upright)
                {
                    kept = true;
                    continue;
                }
                rule.Relocate(rule.Context, world, member, frame);
            }
        return kept;
    }

    std::uint32_t AdvanceSerial(World& world, EntityId entity)
    {
        if (RelocationSerial* serial = world.TryGet<RelocationSerial>(entity))
            return ++serial->Value;
        world.AddComponent<RelocationSerial>(entity, RelocationSerial{ .Value = 1 });
        return 1;
    }
}

RelocationResult RelocateEntity(World& world, EntityId root, const RigidRelocation& relocation)
{
    if (!world.IsAlive(root))
        return RelocationResult{ .Outcome = RelocationOutcome::Gone };
    if (world.IsRegistered<Parent>() && world.HasComponent<Parent>(root))
        return RelocationResult{ .Outcome = RelocationOutcome::Parented };
    if (!IsRigidMapping(relocation.Mapping))
        return RelocationResult{ .Outcome = RelocationOutcome::NotRigid };

    const std::vector<EntityId> group = RootAndDescendants(world, root);
    const std::span<const ComponentRule> rules = RulesOf(world);
    if (const std::string_view refusing = RefusingName(world, rules, group); !refusing.empty())
        return RelocationResult{ .Outcome = RelocationOutcome::Refused, .Component = refusing };

    if (relocation.Partition.has_value())
        MoveToPartition(world, group, *relocation.Partition);

    const bool upright = KeepsWorldUp(relocation.Mapping.Rotation);
    const RelocationFrame frame{ .Mapping = relocation.Mapping,
                                 .Yaw = upright ? YawAboutWorldUp(relocation.Mapping.Rotation) : 0.0f,
                                 .Upright = upright,
                                 .TickFraction = std::clamp(relocation.TickFraction, 0.0f, 1.0f) };

    RelocationResult result;
    result.KeptUprightState = RunComponentRules(world, rules, group, frame, RelocationRuleKind::Maps);
    if (const RelocationRules* registered = world.TryGetResource<RelocationRules>())
        for (const RelocationRules::GroupRule& rule : registered->GroupRules())
            rule.Relocate(rule.Context, world, group, frame);
    (void)RunComponentRules(world, rules, group, frame, RelocationRuleKind::Mirrors);

    FrameRelocations* record = world.TryGetResource<FrameRelocations>();
    if (record == nullptr)
        record = &world.AddResource<FrameRelocations>();
    for (const EntityId member : group)
    {
        const std::uint32_t serial = world.IsRegistered<RelocationSerial>() ? AdvanceSerial(world, member) : 0;
        record->Record(FrameRelocation{
            .Entity = member, .Serial = serial, .Mapping = frame.Mapping, .TickFraction = frame.TickFraction });
        if (member == root)
            result.Serial = serial;
    }
    return result;
}

std::string_view RefusingComponent(const World& world, EntityId root)
{
    if (!world.IsAlive(root))
        return {};
    return RefusingName(world, RulesOf(world), RootAndDescendants(world, root));
}

void ClearFrameRelocations(World& world)
{
    if (FrameRelocations* record = world.TryGetResource<FrameRelocations>())
        record->Clear();
}

std::uint32_t RelocationSerialOf(const World& world, EntityId entity)
{
    const RelocationSerial* serial =
        world.IsRegistered<RelocationSerial>() ? world.TryGet<RelocationSerial>(entity) : nullptr;
    return serial != nullptr ? serial->Value : 0;
}

void RelocationRules::Set(const ComponentRule& rule)
{
    const auto existing = std::ranges::find(Components, rule.Component, &ComponentRule::Component);
    if (existing != Components.end())
        *existing = rule;
    else
        Components.push_back(rule);
}

void RelocationRules::AddGroupRule(const GroupRule& rule)
{
    const bool known = std::ranges::any_of(Groups, [&](const GroupRule& group) {
        return group.Relocate == rule.Relocate && group.Context == rule.Context;
    });
    if (!known)
        Groups.push_back(rule);
}

RelocationRules& EnsureRelocationRules(World& world)
{
    if (RelocationRules* rules = world.TryGetResource<RelocationRules>())
        return *rules;
    return world.AddResource<RelocationRules>();
}
