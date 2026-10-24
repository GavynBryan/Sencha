#pragma once

#include <core/metadata/EditorVisual.h>
#include <core/metadata/EnumSchema.h>
#include <ecs/ComponentAnnotations.h>
#include <ecs/EntityId.h>
#include <logic/VerbSequenceKey.h>

#include <array>
#include <cstdint>
#include <optional>
#include <type_traits>

class World;

// When an entity's sensor runs its sequence: authored policy only. What a
// trigger did at runtime is TriggerSystem's transient state, so a zone that
// streams back comes back armed as authored. docs/gameplay/triggers.md.

enum class TriggerActivation : std::uint8_t
{
    // An activator's first sample inside the volume, a fast crossing included.
    Enter,
    // On entry, then every stay interval while the activator remains inside.
    Stay,
    // An activator leaving, a fast crossing included.
    Exit,
    // An activator inside asking, through trigger.interact.
    Interaction,
};

template <>
struct EnumSchema<TriggerActivation>
{
    static constexpr std::array Values = {
        EnumValue{ TriggerActivation::Enter, "enter" },
        EnumValue{ TriggerActivation::Stay, "stay" },
        EnumValue{ TriggerActivation::Exit, "exit" },
        EnumValue{ TriggerActivation::Interaction, "interaction" },
    };
};

enum class TriggerCooldownScope : std::uint8_t
{
    // One cooldown for the whole trigger, whoever activated it.
    Trigger,
    // A cooldown per activator; others still activate it meanwhile.
    Activator,
};

template <>
struct EnumSchema<TriggerCooldownScope>
{
    static constexpr std::array Values = {
        EnumValue{ TriggerCooldownScope::Trigger, "trigger" },
        EnumValue{ TriggerCooldownScope::Activator, "activator" },
    };
};

struct SENCHA_COMPONENT("sencha.trigger")
       SENCHA_SCHEMA("trigger")
Trigger
{
    SENCHA_FIELD("activation")
    SENCHA_LABEL("Activates on")
    SENCHA_TOOLTIP("Which overlap report runs the sequence.")
    TriggerActivation Activation = TriggerActivation::Enter;

    SENCHA_FIELD("enabled")
    SENCHA_LABEL("Starts enabled")
    SENCHA_TOOLTIP("A disabled trigger ignores activators until trigger.enable arms it.")
    bool Enabled = true;

    SENCHA_FIELD("once")
    SENCHA_LABEL("Once")
    SENCHA_TOOLTIP("After running its sequence the trigger removes itself, and stays removed while its zone is unloaded.")
    bool Once = false;

    SENCHA_FIELD("cooldown")
    SENCHA_LABEL("Cooldown")
    SENCHA_TOOLTIP("Seconds after an activation before it can activate again.")
    float CooldownSeconds = 0.0f;

    SENCHA_FIELD("cooldown_scope")
    SENCHA_LABEL("Cooldown applies to")
    TriggerCooldownScope CooldownScope = TriggerCooldownScope::Trigger;

    SENCHA_FIELD("stay_interval")
    SENCHA_LABEL("Stay interval")
    SENCHA_TOOLTIP("Seconds between activations while inside; zero is every tick.")
    float StayIntervalSeconds = 0.0f;

    // The scene form is the sequence's authored text; see TriggerSerializer.
    VerbSequenceKey Sequence{};
};

static_assert(std::is_trivially_copyable_v<Trigger>);

#if !defined(SENCHA_CODEGEN)
#  include <logic/Trigger.sencha.h>
#endif

// Trigger volumes draw orange in the editor, whatever else their volume is.
template <>
struct ComponentEditorVisual<Trigger>
{
    static constexpr std::optional<EditorVisual> Value = EditorVisual{
        .VisualKind = EditorVisual::Kind::VolumeTint,
        .AssetPath = {},
        .Fill = { 0.85f, 0.32f, 0.02f, 0.16f },
        .Edge = { 1.0f, 0.45f, 0.05f, 0.95f },
    };
};
