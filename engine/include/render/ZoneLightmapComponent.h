#pragma once

#include <ecs/ComponentTypeId.h>
#include <render/TextureHandle.h>

#include <cstdint>
#include <string_view>
#include <tuple>

class TextureCache;

//=============================================================================
// ZoneLightmapComponent
//
// One per cooked zone scene: the zone's baked-lighting atlas texture, plus
// the ambient-occlusion plane sharing its layout (invalid when the cook
// baked no AO). The level cook emits it when the zone has baked lights;
// render extraction resolves the textures' bindless indices once per
// registry and stamps them on every draw item from that zone.
//=============================================================================
struct ZoneLightmapComponent
{
    TextureHandle Texture;
    TextureHandle Ao;
};

SENCHA_DECLARE_COMPONENT_TYPE(ZoneLightmapComponent, "ZoneLightmap");
SENCHA_COMPONENT_DECLARES_SCHEMA(ZoneLightmapComponent);
SENCHA_COMPONENT_DECLARES_TRAITS(ZoneLightmapComponent);
