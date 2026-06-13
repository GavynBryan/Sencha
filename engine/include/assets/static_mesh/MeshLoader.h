#pragma once

#include <render/skinned_mesh/SkinnedMeshData.h>
#include <render/static_mesh/MeshGeometry.h>

#include <cstddef>
#include <span>
#include <string_view>

class BinaryReader;
class LoggingProvider;
class Logger;

//=============================================================================
// MeshLoader
//
// The single reader of the mesh binary container (SMSH). The typed entry
// points enforce the static/skinned split at load: the static loads reject a
// file whose skinned flag is set, and the skinned loads require it — a
// caller can never accidentally read a skinned mesh as static geometry or
// vice versa, even though both share one binary format.
//=============================================================================
class MeshLoader
{
public:
    explicit MeshLoader(LoggingProvider& logging);

    // Static geometry: rejects a file with the skinned flag set.
    [[nodiscard]] bool LoadFromFile(std::string_view path, MeshGeometry& out);
    [[nodiscard]] bool LoadFromBytes(std::span<const std::byte> bytes, MeshGeometry& out);

    // Skinned mesh: requires the skinned flag; fills geometry + skinning.
    [[nodiscard]] bool LoadSkinnedFromFile(std::string_view path, SkinnedMeshData& out);
    [[nodiscard]] bool LoadSkinnedFromBytes(std::span<const std::byte> bytes, SkinnedMeshData& out);

private:
    // Shared reader. `outSkinning` is null for the static path (skinned files
    // are then rejected) and non-null for the skinned path (static files are
    // then rejected).
    [[nodiscard]] bool LoadFromReader(BinaryReader& reader,
                                      size_t fileSize,
                                      std::string_view sourceName,
                                      MeshGeometry& outGeometry,
                                      MeshSkinning* outSkinning);
    [[nodiscard]] bool LoadFromBytesImpl(std::span<const std::byte> bytes,
                                         std::string_view sourceName,
                                         MeshGeometry& outGeometry,
                                         MeshSkinning* outSkinning);
    [[nodiscard]] bool LoadFromFileImpl(std::string_view path,
                                        MeshGeometry& outGeometry,
                                        MeshSkinning* outSkinning);

    Logger& Log;
};
