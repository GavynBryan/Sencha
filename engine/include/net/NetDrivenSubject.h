#pragma once

#include <ecs/EntityId.h>
#include <net/NetSession.h>

class World;

// Read-only network query. Mutation belongs to SessionParticipantProjection.
[[nodiscard]] EntityId NetDrivenSubjectForPeer(const World& world, PeerId peer);
