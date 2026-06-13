#pragma once

#include <render/skinned_mesh/SkinnedMeshData.h>
#include <render/static_mesh/MeshGeometry.h>

#include <cstddef>
#include <string_view>
#include <vector>

class BinaryWriter;
class LoggingProvider;
class Logger;

//=============================================================================
// MeshSerializer
//
// The single writer of the mesh binary container (the SMSH format). One
// format serves both static and skinned meshes — the skinned flag and an
// optional trailing skinning chunk are a property of the *bytes*, not of the
// runtime type system, which keeps static and skinned as distinct asset
// types (Decisions J, M). Static meshes write a `.smesh`; skinned meshes
// write a `.skmesh` (same magic, skinned flag set) so the kind is known from
// the path without reading the payload.
//=============================================================================
class MeshSerializer
{
public:
    explicit MeshSerializer(LoggingProvider& logging);

    // Static geometry — `.smesh`, skinned flag clear, no skinning chunk.
    [[nodiscard]] bool WriteToFile(std::string_view path, const MeshGeometry& mesh);
    [[nodiscard]] bool WriteToBytes(const MeshGeometry& mesh, std::vector<std::byte>& out);

    // Skinned mesh — `.skmesh`, skinned flag set, geometry + skinning chunk.
    [[nodiscard]] bool WriteSkinnedToFile(std::string_view path, const SkinnedMeshData& mesh);
    [[nodiscard]] bool WriteSkinnedToBytes(const SkinnedMeshData& mesh, std::vector<std::byte>& out);

private:
    // Shared geometry-writing core; `skinning` is null for static meshes and
    // appended as the trailing chunk when present.
    [[nodiscard]] bool WriteToWriter(BinaryWriter& writer,
                                     const MeshGeometry& geometry,
                                     const MeshSkinning* skinning);

    Logger& Log;
};
