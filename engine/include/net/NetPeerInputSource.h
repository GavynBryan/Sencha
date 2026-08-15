#pragma once

#include <input/InputActionSource.h>
#include <net/NetSession.h>

#include <cstdint>
#include <unordered_map>

class World;

struct NetPeerSources
{
    std::unordered_map<std::uint32_t, InputActionSourceId> Sources;
};

[[nodiscard]] InputActionSourceId NetSourceForPeer(World& world, PeerId peer);
[[nodiscard]] InputActionSourceId NetFindSourceForPeer(const World& world,
                                                       PeerId peer);

// Closes and forgets the per-peer source. SessionParticipantProjection calls
// this as part of departure so source lifetime cannot drift from participation.
void NetReleasePeerSource(World& world, PeerId peer);
