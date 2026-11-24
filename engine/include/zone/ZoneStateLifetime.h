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

// Whether a save keeps an entity's transform: docs/gameplay/saves.md, "Placement".
enum class ZoneStatePlacement : std::uint8_t
{
    ByComponents,
    Kept,
    Authored,
};

template <>
struct EnumSchema<ZoneStatePlacement>
{
    static constexpr std::array Values = {
        EnumValue{ ZoneStatePlacement::ByComponents, "by_components", "By its components",
                   "Kept when something on it moves it in a way nothing can recompute, such as a dynamic body." },
        EnumValue{ ZoneStatePlacement::Kept, "kept", "Kept", "Kept wherever it is moved to." },
        EnumValue{ ZoneStatePlacement::Authored, "authored", "Where the level put it",
                   "Back where the level put it, however it was moved." },
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

    SENCHA_FIELD("placement")
    SENCHA_LABEL("Placement")
    SENCHA_TOOLTIP("Whether a save keeps where it is: by its components (a dynamic body's place is kept), always, or never.")
    ZoneStatePlacement Placement = ZoneStatePlacement::ByComponents;
};

#if !defined(SENCHA_CODEGEN)
#  include <zone/ZoneStateLifetime.sencha.h>
#endif
