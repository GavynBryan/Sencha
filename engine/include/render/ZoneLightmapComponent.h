#pragma once

#include <ecs/ComponentAnnotations.h>
#include <ecs/ComponentTraits.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>
#include <render/TextureHandle.h>

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

// Defined in ZoneLightmapComponent.cpp. Retaining a texture needs the cache's
// definition, and this header is pulled in by scene serialization, schema
// startup, and the editor's queue builder -- none of which should acquire a
// dependency on the Vulkan-side texture cache to name a component. Sibling
// components can inline their hooks because their caches (StaticMeshCache,
// MaterialSetCache) live under render/; TextureCache does not.
template <>
struct ComponentTraits<ZoneLightmapComponent>
{
    static void OnAdd(ZoneLightmapComponent& component, World& world, EntityId);
    static void OnRemove(const ZoneLightmapComponent& component, World& world,
                         EntityId);
};

SENCHA_COMPONENT_DECLARES_TRAITS(ZoneLightmapComponent);

#if !defined(SENCHA_CODEGEN)
#  include <render/ZoneLightmapComponent.sencha.h>
#endif
