#pragma once

#include <assets/data/DataAssetHandle.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <core/metadata/DataSchema.h>
#include <render/MaterialSetCache.h>
#include <render/static_mesh/StaticMeshHandle.h>

#include <string>
#include <vector>

// The pawn's world representation: the mesh a player body is drawn with and the
// materials it is drawn in. Authored as data so a project chooses its own body
// without rebuilding the game module.
//
// Paths rather than handles: a data asset compiles inside the editor as well as
// the runtime, and the editor has no runtime asset caches. The game resolves
// these against its own caches when it spawns a pawn.
struct CompiledPlayerAvatar
{
    std::string MeshPath;
    std::vector<std::string> MaterialPaths;
};

struct PlayerAvatarHandle
{
    DataAssetHandle Value;

    [[nodiscard]] bool IsValid() const { return Value.IsValid(); }
    auto operator<=>(const PlayerAvatarHandle&) const = default;
};

// The authored paths resolved against the runtime caches. Both handles must be
// valid to draw anything: a mesh with no materials has nothing to draw it with.
struct ResolvedPlayerAvatar
{
    StaticMeshHandle Mesh;
    MaterialSetHandle Materials;

    [[nodiscard]] bool IsValid() const
    {
        return Mesh.IsValid() && Materials.IsValid();
    }
};

void RegisterPlayerAvatarData(DataAssetTypeRegistry& types,
                              DataSchemaRegistry& schemas);
void UnregisterPlayerAvatarData(DataAssetTypeRegistry& types,
                                DataSchemaRegistry& schemas);
