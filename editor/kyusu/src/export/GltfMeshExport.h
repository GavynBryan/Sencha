#pragma once

#include <core/assets/AssetRef.h>
#include <assets/static_mesh/MeshGeometry.h>

#include <filesystem>
#include <span>
#include <string>

// Writes MeshGeometry as a binary glTF (.glb): one mesh with one primitive per
// section (POSITION / NORMAL / TEXCOORD_0 / TANGENT + indices), one material
// stub per entry of materialOrder (named from the asset path stem, so a DCC
// import shows which engine material each slot maps to; the .smat contents are
// not translated). Engine and glTF share right-handed +Y-up axes but face
// opposite ways (GltfFrame.h), so the vertex data is written as-is under a
// node that turns it into glTF's frame. Editor-side only; the runtime has no
// exporter.
[[nodiscard]] bool WriteGlbFile(const MeshGeometry& geometry,
                                std::span<const AssetRef> materialOrder,
                                const std::filesystem::path& path,
                                std::string* error);
