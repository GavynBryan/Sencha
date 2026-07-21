#pragma once

#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>
#include <gameplay_tags/GameplayTagId.h>

#include <type_traits>
#include <vector>

class StoragePartitionSet;
class World;
struct FixedLogicContext;

struct LocomotionModeRequest
{
    ComponentTypeId Marker;
    int Priority = 0;
};

static_assert(std::is_trivially_copyable_v<LocomotionModeRequest>);
SENCHA_DECLARE_COMPONENT_TYPE(
    LocomotionModeRequest,
    "sencha.locomotion_mode_request");

struct LocomotionModeEntry
{
    ComponentTypeId Marker;
    GameplayTagId ActiveTag;
};

class LocomotionModeRegistry
{
public:
    void Register(ComponentTypeId marker, GameplayTagId activeTag);
    [[nodiscard]] const std::vector<LocomotionModeEntry>& Entries() const
    {
        return Modes;
    }
    [[nodiscard]] GameplayTagId TagFor(ComponentTypeId marker) const;

private:
    std::vector<LocomotionModeEntry> Modes;
};

template <typename Marker>
void RegisterLocomotionMode(
    LocomotionModeRegistry& registry,
    GameplayTagId activeTag)
{
    registry.Register(ResolveComponentTypeId<Marker>(), activeTag);
}

void RequestLocomotionMode(
    World& world,
    EntityId entity,
    ComponentTypeId marker,
    int priority);

void ApplyLocomotionModes(World& world);
void ApplyLocomotionModes(
    World& world,
    const StoragePartitionSet& partitions);

class LocomotionModeArbiter
{
public:
    void FixedLogic(FixedLogicContext& ctx);
};
