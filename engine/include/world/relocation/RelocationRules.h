#pragma once

#include <ecs/ComponentId.h>
#include <ecs/EntityId.h>
#include <ecs/World.h>
#include <math/geometry/3d/Transform3d.h>

#include <cstdint>
#include <span>
#include <vector>

struct RelocationFrame
{
    Transform3f Mapping = Transform3f::Identity();
    // Radians about world up; meaningful only when Upright.
    float Yaw = 0.0f;
    bool Upright = true;
    float TickFraction = 0.0f;
    // Root first.
    std::span<const EntityId> Group;

    [[nodiscard]] Vec3d MapPoint(const Vec3d& point) const { return Mapping.TransformPoint(point); }
    [[nodiscard]] Vec3d MapVector(const Vec3d& vector) const { return Mapping.Rotation.RotateVector(vector); }
    [[nodiscard]] Transform3f MapPose(const Transform3f& pose) const { return Mapping * pose; }
};

enum class RelocationRuleKind : std::uint8_t
{
    Maps,
    MapsWhenUpright,
    // After every Maps rule: a backend's copy of state those rules relocated.
    Mirrors,
    Refuses,
};

using RelocateComponentFn = void (*)(void* context, World& world, EntityId entity, const RelocationFrame& frame);
using RelocateGroupFn = void (*)(void* context, World& world, std::span<const EntityId> group,
                                 const RelocationFrame& frame);

// A World resource (docs/gameplay/relocation.md). Rules run on the owner
// thread outside any query and may not change archetypes.
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

    // One rule per component: a second replaces the first.
    void Set(const ComponentRule& rule);
    // Once per relocation, after the Maps rules. Idempotent.
    void AddGroupRule(const GroupRule& rule);

    [[nodiscard]] std::span<const ComponentRule> ComponentRules() const { return Components; }
    [[nodiscard]] std::span<const GroupRule> GroupRules() const { return Groups; }

private:
    std::vector<ComponentRule> Components;
    std::vector<GroupRule> Groups;
};

RelocationRules& EnsureRelocationRules(World& world);

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

template <typename T, void (*Relocate)(T&, const RelocationFrame&)>
void RelocateValue(void*, World& world, EntityId entity, const RelocationFrame& frame)
{
    if (T* value = world.TryGet<T>(entity))
        Relocate(*value, frame);
}
