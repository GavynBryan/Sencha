#pragma once

#include <ecs/ComponentAnnotations.h>
#include <ecs/ComponentTraits.h>
#include <ecs/ComponentTypeId.h>
#include <render/TextureHandle.h>
#include <world/ComponentAssetOwnership.h>

//=============================================================================
// ZoneLightmapComponent
//
// One per cooked zone scene: the zone's baked-lighting atlas texture, plus
// the ambient-occlusion plane sharing its layout (invalid when the cook
// baked no AO). The level cook emits it when the zone has baked lights;
// render extraction resolves the textures' bindless indices once per
// registry and stamps them on every draw item from that zone.
//=============================================================================
struct SENCHA_COMPONENT("ZoneLightmap")
       SENCHA_SCHEMA("ZoneLightmap")
       SENCHA_SCENE_CHUNK("ZLMP")
ZoneLightmapComponent
{
    SENCHA_FIELD("texture")
    SENCHA_ASSET(Texture)
    TextureHandle Texture;

    SENCHA_FIELD("ao")
    SENCHA_ASSET(Texture)
    TextureHandle Ao{};
};

#if !defined(SENCHA_CODEGEN)
#  include <render/ZoneLightmapComponent.sencha.h>
#endif

template <>
struct ComponentTraits<ZoneLightmapComponent> : SchemaAssetOwnership<ZoneLightmapComponent> {};
