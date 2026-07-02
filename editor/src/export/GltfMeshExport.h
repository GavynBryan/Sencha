#pragma once

#include <core/assets/AssetRef.h>
#include <render/static_mesh/MeshGeometry.h>

#include <filesystem>
#include <span>
#include <string>

// Writes MeshGeometry as a binary glTF (.glb): one mesh with one primitive per
// section (POSITION / NORMAL / TEXCOORD_0 / TANGENT + indices), one material
// stub per entry of materialOrder (named from the asset path stem, so a DCC
// import shows which engine material each slot maps to; the .smat contents are
// not translated). Engine and glTF share the same right-handed +Y-up frame, so
// geometry is written as-is. Editor-side only; the runtime has no exporter.
[[nodiscard]] bool WriteGlbFile(const MeshGeometry& geometry,
                                std::span<const AssetRef> materialOrder,
                                const std::filesystem::path& path,
                                std::string* error);
