#pragma once

#include <core/metadata/EnumSchema.h>
#include <ecs/ComponentAnnotations.h>

#include <array>
#include <cstdint>

// When a zone forgets that an authored entity was destroyed or changed:
// docs/plans/runtime-stable-identity.md, "Zone state lifetime".
enum class ZoneStateForgetting : std::uint8_t
{
    Never,
    AtRestore,
    AtDetach,
};

template <>
struct EnumSchema<ZoneStateForgetting>
{
    static constexpr std::array Values = {
        EnumValue{ ZoneStateForgetting::Never, "never" },
        EnumValue{ ZoneStateForgetting::AtRestore, "at_restore" },
        EnumValue{ ZoneStateForgetting::AtDetach, "at_detach" },
    };
};

// Authored on an entity, it holds for the entity's children and for the rest of
// the placed scene it belongs to, unless one of them says otherwise.
struct SENCHA_COMPONENT("sencha.zone_state_lifetime")
       SENCHA_SCHEMA("ZoneStateLifetime")
       SENCHA_SCENE_CHUNK("ZSLT")
ZoneStateLifetime
{
    SENCHA_FIELD("forgotten")
    SENCHA_LABEL("Forgotten")
    SENCHA_TOOLTIP("When the zone forgets this was destroyed or changed: never, when a save is restored, or when the zone unloads.")
    ZoneStateForgetting Forgotten = ZoneStateForgetting::AtRestore;
};

#if !defined(SENCHA_CODEGEN)
#  include <zone/ZoneStateLifetime.sencha.h>
#endif
