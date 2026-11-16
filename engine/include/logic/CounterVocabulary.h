#pragma once

#include <authored/AuthoredApiDefinition.h>
#include <authored/AuthoredEventRegistry.h>
#include <authored/AuthoredQueryDispatcher.h>
#include <authored/AuthoredValue.h>
#include <authored/VerbDispatcher.h>
#include <gameplay_tags/GameplayTagId.h>

#include <cstdint>
#include <string_view>
#include <vector>

class AuthoredEventDispatcher;
class Logger;
class World;

inline constexpr std::string_view kCounterSetVerb = "counter.set";
inline constexpr std::string_view kCounterAddVerb = "counter.add";
inline constexpr std::string_view kCounterValueQuery = "counter.value";
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

// An invalid holder is the world. Zero when unset.
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
    // The bound the value was clamped to, when it lay beyond one.
    CounterBound Saturated = CounterBound::None;
};

// Authority only. Zero removes the counter's record.
CounterWrite WriteCounter(World& world, GameplayTagId name, EntityId holder, std::int64_t value);

// Writes clamped to a bound, by bound. A World resource.
struct CounterSaturation
{
    std::uint64_t AtMaximum = 0;
    std::uint64_t AtMinimum = 0;
};

class CounterOperations
{
public:
    // `events` may be null; nothing is then announced.
    CounterOperations(World& world, Logger& log, AuthoredEventDispatcher* events);

    VerbAdmission Set(const VerbInvocation& invocation);
    VerbAdmission Add(const VerbInvocation& invocation);

private:
    VerbAdmission Write(const VerbInvocation& invocation, GameplayTagId name, EntityId holder, std::int64_t value);

    World* Entities;
    Logger* Log;
    AuthoredEventDispatcher* Events;
};

inline VerbAdmission InvokeCounterSet(CounterOperations& operations, const VerbInvocation& invocation)
{
    return operations.Set(invocation);
}

inline VerbAdmission InvokeCounterAdd(CounterOperations& operations, const VerbInvocation& invocation)
{
    return operations.Add(invocation);
}

[[nodiscard]] AuthoredQueryBindingToken BindCounterQuery(AuthoredQueryDispatcher& dispatcher, const World& world);
