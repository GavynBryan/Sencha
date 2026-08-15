#pragma once

#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>
#include <input/InputActionSource.h>

#include <cstdint>
#include <type_traits>

// The runtime control state of one participant. This is independent of where
// the input source came from: local devices, a network peer, a bot, a replay,
// or a script all use the same mechanism.
struct ParticipantControl
{
    InputActionSourceId Source = kLocalInputActionSource;
    EntityId Body;
    EntityId ControlSubject;
};

static_assert(std::is_trivially_copyable_v<ParticipantControl>);
SENCHA_DECLARE_COMPONENT_TYPE(ParticipantControl, "sencha.participant_control");

// At most one participant is presented by this machine. Remote peers and bots
// are simulated participants, not local ones.
struct LocalParticipant
{
};

static_assert(std::is_empty_v<LocalParticipant>);
SENCHA_DECLARE_COMPONENT_TYPE(LocalParticipant, "sencha.local_participant");

enum class ParticipantPresence : std::uint8_t
{
    Simulated,
    Local,
};

