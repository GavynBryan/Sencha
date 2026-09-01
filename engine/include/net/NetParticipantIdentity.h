#pragma once

#include <ecs/ComponentAnnotations.h>
#include <ecs/EntityId.h>
#include <net/NetReplicationComponents.h>
#include <net/NetSession.h>

#include <cstdint>
#include <type_traits>

class World;

// The network projection of a participant: which peer names it on the wire.
// Generic participant state deliberately has no peer concept.
struct SENCHA_COMPONENT("sencha.net_participant_identity")
       SENCHA_SCHEMA("NetParticipantIdentity")
       SENCHA_REPLICATED
NetParticipantIdentity
{
    SENCHA_FIELD("peer")
    std::uint32_t Peer = kNetAuthorityPeer;
};

static_assert(std::is_trivially_copyable_v<NetParticipantIdentity>);

#if !defined(SENCHA_CODEGEN)
#  include <net/NetParticipantIdentity.sencha.h>
#endif

// A valid peer has at most one projected participant. Peer zero is deliberately
// not searchable: it represents local participants, bots, and the authority,
// and therefore is not an identity.
[[nodiscard]] EntityId NetParticipantForPeer(const World& world, PeerId peer);
