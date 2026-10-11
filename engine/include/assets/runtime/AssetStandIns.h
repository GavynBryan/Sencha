#pragma once

#include <assets/texture/TextureData.h>
#include <core/assets/AssetLease.h>
#include <render/Material.h>

#include <cstdint>
#include <string_view>

class MaterialCache;
class TextureCache;

// What a reference to an asset that cannot be loaded draws as: a pink and
// black checker, unmistakable in any light (docs/assets/pipeline.md, Stand-ins).
[[nodiscard]] TextureData StandInTexture();

// Unlit and two-sided, so no light or facing hides it. Without a checker
// (UINT32_MAX) it is flat pink.
[[nodiscard]] Material StandInMaterial(std::uint32_t checkerTextureIndex);

// The two kinds' stand-in operations: each puts its stand-in into the store
// under `path` and yields the creation reference.
[[nodiscard]] AssetLease PutStandInTexture(TextureCache& textures, std::string_view path);
[[nodiscard]] AssetLease PutStandInMaterial(MaterialCache& materials, TextureCache* textures, std::string_view path);
