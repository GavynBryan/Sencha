#pragma once

#include <ecs/ComponentId.h>
#include <ecs/EntityId.h>
#include <ecs/World.h>
#include <math/geometry/3d/Transform3d.h>

#include <cstdint>
#include <span>
#include <vector>

// What one relocation does, as a component's rule sees it.
struct RelocationFrame
{
    // Old space to new space; rigid.
    Transform3f Mapping = Transform3f::Identity();
    // Radians about world up. Rules that need it run only when Upright.
    float Yaw = 0.0f;
    bool Upright = true;
    // Where in the fixed tick the entity crossed, from 0 to 1.
    float TickFraction = 1.0f;

    [[nodiscard]] Vec3d MapPoint(const Vec3d& point) const { return Mapping.TransformPoint(point); }
    [[nodiscard]] Vec3d MapVector(const Vec3d& vector) const { return Mapping.Rotation.RotateVector(vector); }
    [[nodiscard]] Transform3f MapPose(const Transform3f& pose) const { return Mapping * pose; }
};

enum class RelocationRuleKind : std::uint8_t
{
    // Runs under every rigid mapping.
    Maps,
    // Runs only under a mapping that keeps world up; otherwise the state is kept as it was.
    MapsWhenUpright,
    // Runs after every Maps rule of the group: a backend's copy of state those rules relocated.
    Mirrors,
    // The component holds world-space state no relocation is defined for: an entity carrying it is refused.
    Refuses,
};

using RelocateComponentFn = void (*)(void* context, World& world, EntityId entity, const RelocationFrame& frame);
using RelocateGroupFn = void (*)(void* context, World& world, std::span<const EntityId> group,
                                 const RelocationFrame& frame);

// A World resource: how each component holding world-space state relocates
// (docs/gameplay/relocation.md). A component with no rule holds none. Rules run
// on the owner thread, outside any query, and may not change archetypes.
class RelocationRules
{
public:
    struct ComponentRule
    {
        ComponentId Component = InvalidComponentId;
        RelocationRuleKind Kind = RelocationRuleKind::Maps;
        RelocateComponentFn Relocate = nullptr;
        void* Context = nullptr;
    };

    struct GroupRule
    {
        RelocateGroupFn Relocate = nullptr;
        void* Context = nullptr;
    };

    // One rule per component: a second for the same component replaces the first in place.
    void Set(const ComponentRule& rule);
    // Runs once per relocation with the whole group, after its Maps rules. Adding the same rule twice keeps one.
    void AddGroupRule(const GroupRule& rule);

    [[nodiscard]] std::span<const ComponentRule> ComponentRules() const { return Components; }
    [[nodiscard]] std::span<const GroupRule> GroupRules() const { return Groups; }

private:
    std::vector<ComponentRule> Components;
    std::vector<GroupRule> Groups;
};

RelocationRules& EnsureRelocationRules(World& world);

// For a World that registered T; nothing otherwise, so a partial vocabulary stays valid.
template <typename T>
void SetRelocationRule(World& world, RelocationRuleKind kind, RelocateComponentFn relocate, void* context = nullptr)
{
    if (!world.IsRegistered<T>())
        return;
    EnsureRelocationRules(world).Set(RelocationRules::ComponentRule{
        .Component = world.GetComponentId<T>(), .Kind = kind, .Relocate = relocate, .Context = context });
}

template <typename T>
void RefuseRelocation(World& world)
{
    SetRelocationRule<T>(world, RelocationRuleKind::Refuses, nullptr);
}

// A rule that is a function of the component's own value.
template <typename T, void (*Relocate)(T&, const RelocationFrame&)>
void RelocateValue(void*, World& world, EntityId entity, const RelocationFrame& frame)
{
    if (T* value = world.TryGet<T>(entity))
        Relocate(*value, frame);
}
