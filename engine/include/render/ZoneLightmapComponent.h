#pragma once

#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>
#include <ecs/ComponentTraits.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>
#include <ecs/World.h>
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

struct ZoneLightmapComponentAssets
{
    ZoneLightmapComponentAssets() = default;
    explicit ZoneLightmapComponentAssets(TextureCache* textures)
        : Textures(textures)
    {
    }

    TextureCache* Textures = nullptr;
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
            MakeField("ao", &ZoneLightmapComponent::Ao)
                .AsAsset(AssetType::Texture)
                .Default(TextureHandle{}),
        };
    }
};

// Stated rather than derived from TypeSchema::Name, so the schema can move
// without the identity moving with it. The name is repeated exactly.
SENCHA_DECLARE_COMPONENT_TYPE(ZoneLightmapComponent, "ZoneLightmap");
