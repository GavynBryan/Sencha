#pragma once

#include <ecs/EntityId.h>
#include <math/geometry/3d/Transform3d.h>
#include <zone/ZoneStateScope.h>

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class Logger;
class ResponsesSystem;
class RuntimeWorld;
class World;

enum class CarryRefusal : std::uint8_t
{
    None,
    // The loaded level has no landmark of that name, or none was named.
    NoLandmark,
    // Gone, a held record, a body, parented, with children and not the root of a scene instance, or holding
    // state a relocation refuses.
    NotCarriable,
};

[[nodiscard]] CarryRefusal RefuseCarrying(const World& world, std::span<const EntityId> entities,
                                          std::string_view landmark);

// What a level change carries besides its bodies: docs/gameplay/game-module.md, "Carrying".
class LevelCarry
{
public:
    using ScopeOf = std::function<ZoneStateScope(EntityId)>;

    // Before the level it leaves unloads. Each entity and its descendants leave for the holding partition,
    // the landmark's place is kept, and its zone records it departed.
    void TakeOut(RuntimeWorld& runtime, std::span<const EntityId> entities, std::string landmark,
                 const ScopeOf& scopeOf);
    // Once the level it enters is resident: each is relocated from the landmark it left to the landmark of the
    // same name (RelocateEntity), into the zone holding its new place, as a member that zone keeps. Nothing is
    // held after it.
    void Arrive(RuntimeWorld& runtime, ResponsesSystem* responses, Logger& log);
    // A change that failed or was replaced: what is held is destroyed and logged.
    void Drop(RuntimeWorld& runtime, Logger& log, std::string_view why);

    [[nodiscard]] bool Holds() const { return !Held.empty(); }

private:
    struct Carried
    {
        EntityId Root;
        std::vector<EntityId> Group;
    };

    std::string Landmark;
    Transform3f Left = Transform3f::Identity();
    std::vector<Carried> Held;
};
