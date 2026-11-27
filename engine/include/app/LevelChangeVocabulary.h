#pragma once

#include <app/LevelChange.h>
#include <authored/AuthoredAnnotations.h>
#include <authored/AuthoredApiDefinition.h>
#include <authored/AuthoredEventRegistry.h>
#include <authored/AuthoredValue.h>
#include <authored/VerbInvocation.h>
#include <authored/VerbRegistry.h>
#include <authored/VerbResult.h>
#include <ecs/EntityId.h>
#include <world/LevelReference.h>

#include <optional>
#include <string_view>

class World;

inline constexpr std::string_view kLevelArrivedEvent = "level.arrived";

// Delivered at the landmark an entity a level change carried arrives at, once for each, the entity as instigator.
struct LevelArrived
{
    EntityId Instigator{};
};

template <>
struct AuthoredApiDefinition<LevelArrived>
{
    static constexpr std::string_view EventName = kLevelArrivedEvent;
    static void Encode(const LevelArrived& event, AuthoredArguments& payload)
    {
        payload.Resize(1);
        payload.Set(0, AuthoredValue::Entity(event.Instigator));
    }
};

void DeclareLevelChangeEvents(AuthoredEventRegistrationScope& events);

class LevelChangeOperations
{
public:
    // `entities` is the World a `carry_within` volume's occupants are read from.
    LevelChangeOperations(LevelChange& changes, const World& entities)
        : Changes(&changes)
        , Entities(&entities)
    {
    }

    SENCHA_VERB("level.change")
    SENCHA_LABEL("Change level")
    SENCHA_CATEGORY("Levels")
    SENCHA_DESCRIPTION("Replaces the loaded level with another, or the same one again, saying what it keeps.")
    VerbRefusal<LevelChangeAdmission> Change(
        SENCHA_ARG("level") SENCHA_LABEL("Level") SENCHA_DESCRIPTION("The level's name, as `map` or `world` takes it.")
            std::string_view level,
        SENCHA_ARG("memory") SENCHA_LABEL("Memory")
            SENCHA_DESCRIPTION("Remembered enters it as the game left it; authored as content made it.")
                LevelMemory memory,
        SENCHA_ARG("bodies") SENCHA_LABEL("Bodies")
            SENCHA_DESCRIPTION("Kept carries the same bodies across; fresh spawns the game's prefab.")
                LevelChangeBodies bodies,
        SENCHA_ARG("kind") SENCHA_LABEL("Kind") SENCHA_DESCRIPTION("A single scene, or a partitioned world.")
            LevelKind kind = LevelKind::Scene,
        SENCHA_ARG("arrival") SENCHA_LABEL("Arrival")
            SENCHA_DESCRIPTION("The landmark bodies arrive at; the game's spawn when empty.") std::string_view arrival = "",
        SENCHA_ARG("landmark") SENCHA_LABEL("Landmark")
            SENCHA_DESCRIPTION("A landmark both levels have: what crosses keeps its place from it.")
                std::string_view landmark = "",
        SENCHA_ARG("carry_within") SENCHA_LABEL("Carry within")
            SENCHA_DESCRIPTION("A volume whose occupants cross with the bodies, by the landmark.")
                std::optional<EntityId> carryWithin = std::nullopt);

private:
    LevelChange* Changes;
    const World* Entities;
};

#if !defined(SENCHA_CODEGEN)
#  include <app/LevelChangeVocabulary.sencha.h>
#endif
