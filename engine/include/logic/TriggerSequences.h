#pragma once

#include <core/json/JsonValue.h>
#include <ecs/EntityId.h>
#include <logic/VerbSequenceStore.h>

#include <array>
#include <cstdint>

class World;

// The ports a trigger's sequence reads.
inline constexpr std::uint8_t kTriggerActivatorPort = 0;
inline constexpr std::uint8_t kTriggerEntityPort = 1;
inline constexpr std::array<SequencePort, 2> kTriggerPorts = { {
    { "activator", SequencePortKind::Entity },
    { "trigger", SequencePortKind::Entity },
} };

// The World's trigger sequences, created on first use.
struct TriggerSequences
{
    VerbSequenceStore Store{ kTriggerPorts };
};

[[nodiscard]] VerbSequenceStore& TriggerSequencesOf(World& world);

// Replaces the entity's sequence with the one `authored` spells. False when the
// entity carries no Trigger.
bool SetTriggerSequence(World& world, EntityId trigger, const JsonValue& authored);

// The authored text of the entity's sequence, or null.
[[nodiscard]] const JsonValue* TriggerSequenceSource(const World& world, EntityId trigger);
