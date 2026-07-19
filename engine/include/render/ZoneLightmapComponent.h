#pragma once

#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>
#include <render/TextureHandle.h>

#include <cstdint>
#include <string_view>
#include <tuple>

//=============================================================================
// ZoneLightmapComponent
//
// One per cooked zone scene: the zone's baked-lighting atlas texture. The
// level cook emits it when the zone has baked lights; render extraction
// resolves the texture's bindless index once per registry and stamps it on
// every draw item from that zone.
//=============================================================================
struct ZoneLightmapComponent
{
    TextureHandle Texture;
};

template <>
struct TypeSchema<ZoneLightmapComponent>
{
    static constexpr std::string_view Name = "ZoneLightmap";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('Z', 'L', 'M', 'P');

    static auto Fields()
    {
        return std::tuple{
            MakeField("texture", &ZoneLightmapComponent::Texture)
                .AsAsset(AssetType::Texture),
        };
    }
};
