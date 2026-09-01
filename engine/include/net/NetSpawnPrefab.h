#pragma once

#include <core/assets/AssetId.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>
#include <ecs/StoragePartitionId.h>

#include <string_view>
#include <tuple>
#include <type_traits>

class World;

//=============================================================================
// NetSpawnPrefab
//
// What a replicated entity is on the machine receiving it.
//
// A snapshot carries values, so an entity built from one arrives with exactly
// the components that replicate and nothing else -- no derived columns, no
// presentation, no local scaffolding. That is most of what an entity is, and
// left to each machine to reassemble by hand the difference is invisible when
// it is wrong: the entity exists, its state is correct, and nothing draws it.
//
// So the authority names the prefab and the receiver instantiates it. The wire
// carries the identity, never the content: both machines already have the same
// content, which is the assumption the whole posture rests on -- the same one
// that lets a cooked scene's bytes be assumed identical.
//
// The identity is an AssetId, which is minted from the asset's virtual path and
// written into the id map both machines ship. Nothing about it depends on the
// order content was seen in, which is what makes it something a peer can
// resolve rather than a number local to one process.
//=============================================================================
struct NetSpawnPrefab
{
    // Invalid means no prefab: an entity that is nothing but its replicated
    // state, which is a legitimate thing to be.
    AssetId Scene;
};

static_assert(std::is_trivially_copyable_v<NetSpawnPrefab>,
              "NetSpawnPrefab must be trivially copyable to live in ECS chunks");

SENCHA_DECLARE_COMPONENT_TYPE(NetSpawnPrefab, "sencha.net_spawn_prefab");

//=============================================================================
// INetPrefabSpawner
//
// How the snapshot applier makes and unmakes a prefab body, without knowing
// what a scene, an asset system, or a package is.
//
// A seam rather than a call because of what is on the other side: resolving an
// id, holding a scene resident, building a package, and importing it is the
// whole content stack, and a replication test that had to stand one up to
// prove a snapshot applies would be testing something else. The applier states
// what it needs -- can this be built, build it, unbuild it -- and that is all
// three of these.
//=============================================================================

enum class NetPrefabReadiness : std::uint8_t
{
    // Instantiate will produce a root.
    Ready,
    // Not now. The spawn is deferred rather than built bare: the snapshot goes
    // unacknowledged, the authority describes the entity again, and the next
    // attempt finds whatever was missing. An id this build cannot resolve at
    // all takes the same path -- it is content disagreement, and quietly
    // producing a stateful entity with no body is the failure this exists to
    // prevent.
    Unavailable,
};

class INetPrefabSpawner
{
public:
    virtual ~INetPrefabSpawner() = default;

    // Owner thread, while the snapshot is being read: resolves and validates
    // without touching the world. Whatever this answers Ready for, Instantiate
    // is expected to build.
    [[nodiscard]] virtual NetPrefabReadiness Prepare(AssetId scene) = 0;

    // Owner thread, at the write. Returns the group's single root, or an
    // invalid id having created nothing.
    [[nodiscard]] virtual EntityId Instantiate(AssetId scene,
                                               World& world,
                                               StoragePartitionId partition) = 0;

    // Everything Instantiate made for this root. The snapshot names the root;
    // the rest of the group is this machine's own and has to go with it.
    virtual void Despawn(World& world, EntityId root) = 0;
};
