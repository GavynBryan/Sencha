#pragma once

#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>
#include <ecs/ComponentTraits.h>
#include <ecs/EntityId.h>
#include <ecs/World.h>
#include <render/TextureHandle.h>
#include <render/ZoneLightmapComponent.h>

#include <cstdint>
#include <string_view>
#include <tuple>

//=============================================================================
// Authoring shape and atlas ownership for a zone's baked lighting.
//
// Its own unit because its hooks are defined out of line: retaining a texture
// needs the cache, and nothing that names this component should acquire a
// dependency on the Vulkan-side texture cache to do so.
//=============================================================================

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
