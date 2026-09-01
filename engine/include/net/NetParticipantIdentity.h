#pragma once

#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>
#include <net/NetReplicationComponents.h>
#include <net/NetSession.h>

#include <cstdint>
#include <string_view>
#include <tuple>
#include <type_traits>

class World;

// The network projection of a participant: which peer names it on the wire.
// Generic participant state deliberately has no peer concept.
struct NetParticipantIdentity
{
    std::uint32_t Peer = kNetAuthorityPeer;
};

static_assert(std::is_trivially_copyable_v<NetParticipantIdentity>);

SENCHA_DECLARE_COMPONENT_TYPE(NetParticipantIdentity,
                              "sencha.net_participant_identity");

// A valid peer has at most one projected participant. Peer zero is deliberately
// not searchable: it represents local participants, bots, and the authority,
// and therefore is not an identity.
[[nodiscard]] EntityId NetParticipantForPeer(const World& world, PeerId peer);
