#pragma once

#include <core/handle/Handle.h>
#include <core/metadata/EnumSchema.h>
#include <ecs/ComponentAnnotations.h>
#include <ecs/ComponentTraits.h>
#include <type_traits>

using TriggerProgramHandle = Handle<struct TriggerProgramTag>;
enum class TriggerEvent : std::uint8_t
{
    Enter,
    Stay,
    Exit
};

template <> struct EnumSchema<TriggerEvent>
{
    static constexpr std::array Values = {
        EnumValue{TriggerEvent::Enter, "Enter"},
        EnumValue{TriggerEvent::Stay, "Stay"},
        EnumValue{TriggerEvent::Exit, "Exit"},
    };
};

struct SENCHA_COMPONENT("sencha.trigger") SENCHA_SCHEMA("trigger") Trigger
{
    SENCHA_FIELD("enabled")
    bool Enabled = true;
    SENCHA_FIELD("event")
    TriggerEvent Event = TriggerEvent::Enter;
    SENCHA_FIELD("cooldown")
    float Cooldown = 0.0f;
    SENCHA_FIELD("per_activator")
    bool PerActivator = true;
    SENCHA_FIELD("once")
    bool Once = false;
    SENCHA_FIELD("stay_interval")
    float StayInterval = 0.1f;
    TriggerProgramHandle Program;
};

static_assert(std::is_trivially_copyable_v<Trigger>);

template <> struct ComponentTraits<Trigger>
{
    static void OnAdd(Trigger &trigger, World &world, EntityId entity);
    static void OnRemove(const Trigger &trigger, World &world, EntityId entity);
};

#if !defined(SENCHA_CODEGEN)
#include <logic/Trigger.sencha.h>
#endif
