#pragma once

#include <authored/AuthoredApiDefinition.h>
#include <authored/AuthoredEventRegistry.h>
#include <authored/AuthoredValue.h>
#include <authored/VerbInvocation.h>
#include <authored/VerbRegistry.h>
#include <ecs/EntityId.h>

#include <string_view>

class LevelChange;
class World;

inline constexpr std::string_view kLevelChangeVerb = "level.change";
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

void DeclareLevelChangeVerbs(VerbRegistrationScope& verbs, AuthoredEventRegistrationScope& events);
// The verb's state: the level change, and the World a `carry_within` volume's occupants are read from.
struct LevelChangeOperations
{
    LevelChange& Changes;
    const World& Entities;
};

VerbAnswer InvokeLevelChange(LevelChangeOperations& operations, const VerbInvocation& invocation);
