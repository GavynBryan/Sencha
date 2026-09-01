#pragma once

#include <net/NetParticipantIdentity.h>
#include <net/NetComponentSchemas.h>
#include <net/NetReplicationComponents.h>
#include <net/NetSpawnPrefab.h>
#include <world/ComponentRegistrar.h>

// Which entities travel, who takes part, whose inputs drive them, and how a
// client builds one it has never seen.
//
// Present in every build: a World's component vocabulary is fixed before any
// session can exist, so these cannot be added later on the machine that turns
// out to host. They cost a column each and nothing else when no session is
// ever created.
inline void RegisterNetComponents(ComponentRegistrar& registrar)
{
    registrar.Add<NetReplicated>();
    registrar.Add<NetOwner>();
    registrar.Add<NetDrivenBy>();
    registrar.Add<NetSpawnPrefab>();
    registrar.Add<NetParticipantIdentity>();
}
