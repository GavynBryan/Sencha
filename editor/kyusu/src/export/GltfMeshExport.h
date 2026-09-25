#pragma once

#include <core/assets/AssetRef.h>
#include <assets/static_mesh/MeshGeometry.h>

#include <filesystem>
#include <span>
#include <string>

// A .glb with a primitive per section and a material stub per materialOrder entry,
// named by its asset's stem. Vertices go out as-is under a node that turns the
// engine frame into glTF's (GltfFrame.h).
[[nodiscard]] bool WriteGlbFile(const MeshGeometry& geometry,
                                std::span<const AssetRef> materialOrder,
                                const std::filesystem::path& path,
                                std::string* error);
