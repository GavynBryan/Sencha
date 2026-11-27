#pragma once

#include <authored/AuthoredAnnotations.h>
#include <authored/AuthoredApiDefinition.h>
#include <authored/AuthoredEventRegistry.h>
#include <authored/AuthoredQueryRegistry.h>
#include <authored/AuthoredValue.h>
#include <authored/VerbRegistry.h>
#include <gameplay_tags/GameplayTagId.h>

#include <cstdint>
#include <optional>
#include <string_view>

class AuthoredEventDispatcher;
class Logger;
class World;

inline constexpr std::string_view kCounterChangedEvent = "counter.changed";

struct CounterChanged
{
    GameplayTagId Name;
    std::int32_t Value = 0;
    EntityId Holder;
};

template <>
struct AuthoredApiDefinition<CounterChanged>
{
    static constexpr std::string_view EventName = kCounterChangedEvent;

    static void Encode(const CounterChanged& event, AuthoredArguments& payload)
    {
        payload.Resize(3);
        payload.Set(0, AuthoredValue::Tag(event.Name));
        payload.Set(1, AuthoredValue::Int(event.Value));
        payload.Set(2, AuthoredValue::Entity(event.Holder));
    }
};

void DeclareCounterVocabulary(VerbRegistrationScope& verbs, AuthoredQueryRegistrationScope& queries,
                              AuthoredEventRegistrationScope& events);

[[nodiscard]] std::int32_t CounterValue(const World& world, GameplayTagId name, EntityId holder);
[[nodiscard]] EntityId FindCounter(const World& world, GameplayTagId name, EntityId holder);

enum class CounterBound : std::uint8_t
{
    None,
    Maximum,
    Minimum,
};

struct CounterWrite
{
    std::int32_t Value = 0;
    bool Changed = false;
    CounterBound Saturated = CounterBound::None;
};

CounterWrite WriteCounter(World& world, GameplayTagId name, EntityId holder, std::int64_t value);

struct CounterSaturation
{
    std::uint64_t AtMaximum = 0;
    std::uint64_t AtMinimum = 0;
};

class CounterOperations
{
public:
    CounterOperations(World& world, Logger& log, AuthoredEventDispatcher* events);

    SENCHA_VERB("counter.set")
    SENCHA_LABEL("Set counter")
    SENCHA_CATEGORY("Counters")
    SENCHA_DESCRIPTION("Sets a named number, kept by saves. Zero unsets it. Values past a 32-bit integer are held at "
                       "its bound.")
    VerbAnswer Set(const VerbInvocation& invocation,
                      SENCHA_ARG("name") SENCHA_LABEL("Name") SENCHA_DESCRIPTION("Which counter.") GameplayTagId name,
                      SENCHA_ARG("value") SENCHA_LABEL("Value") SENCHA_DESCRIPTION("What it becomes.") std::int64_t value,
                      SENCHA_ARG("holder") SENCHA_LABEL("Holder")
                          SENCHA_DESCRIPTION("Whose counter, such as a participant's body. The world's when absent.")
                              std::optional<EntityId> holder = std::nullopt);

    SENCHA_VERB("counter.add")
    SENCHA_LABEL("Add to counter")
    SENCHA_CATEGORY("Counters")
    SENCHA_DESCRIPTION("Adds to a named number, kept by saves. Reaching zero unsets it. Values past a 32-bit integer "
                       "are held at its bound.")
    VerbAnswer Add(const VerbInvocation& invocation,
                      SENCHA_ARG("name") SENCHA_LABEL("Name") SENCHA_DESCRIPTION("Which counter.") GameplayTagId name,
                      SENCHA_ARG("amount") SENCHA_LABEL("Amount") SENCHA_DESCRIPTION("What is added; negative takes away.")
                          std::int64_t amount = 1,
                      SENCHA_ARG("holder") SENCHA_LABEL("Holder")
                          SENCHA_DESCRIPTION("Whose counter, such as a participant's body. The world's when absent.")
                              std::optional<EntityId> holder = std::nullopt);

    SENCHA_QUERY("counter.value")
    SENCHA_LABEL("Counter value")
    SENCHA_CATEGORY("Counters")
    SENCHA_DESCRIPTION("A named number; zero when it was never set.")
    std::optional<std::int64_t> Value(
        SENCHA_ARG("name") SENCHA_LABEL("Name") SENCHA_DESCRIPTION("Which counter.") GameplayTagId name,
        SENCHA_ARG("holder") SENCHA_LABEL("Holder")
            SENCHA_DESCRIPTION("Whose counter, such as a participant's body. The world's when absent.")
                std::optional<EntityId> holder = std::nullopt) const;

    [[nodiscard]] const World& AuthoredWorld() const { return *Entities; }

private:
    VerbAnswer Admit(GameplayTagId name, std::optional<EntityId> holder) const;
    VerbAdmission Write(const VerbInvocation& invocation, GameplayTagId name, EntityId holder, std::int64_t value);

    World* Entities;
    Logger* Log;
    AuthoredEventDispatcher* Events;
};

#if !defined(SENCHA_CODEGEN)
#  include <logic/CounterVocabulary.sencha.h>
#endif
