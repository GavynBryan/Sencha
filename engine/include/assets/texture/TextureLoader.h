#pragma once

#include <render/TextureData.h>

#include <cstddef>
#include <span>
#include <string>

//=============================================================================
// .stex read half. Pure with respect to engine state (no logging, no
// services) so it can run on async task threads inside LoadStaged —
// failures travel in `error`, mirroring MeshLoader::LoadFromBytes.
//
// The loaded TextureData is re-validated structurally; a truncated or
// inconsistent container is rejected, never patched.
//=============================================================================
[[nodiscard]] bool LoadStexFromBytes(std::span<const std::byte> bytes,
                                     TextureData& out,
                                     std::string* error = nullptr);
