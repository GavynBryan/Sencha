#pragma once

#include <core/assets/AssetId.h>
#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>
#include <ecs/ComponentTraits.h>
#include <ecs/EntityId.h>
#include <ecs/World.h>
#include <world/scene/SceneInstance.h>
#include <world/serialization/SceneFieldCodec.h>

#include <cstdint>
#include <string_view>
#include <tuple>

//=============================================================================
// Authoring shape and instance membership for entities projected from a scene.
//=============================================================================

template <>
struct ComponentTraits<SceneInstance>
{
    static void OnAdd(SceneInstance& component, World& world, EntityId entity);
    static void OnRemove(const SceneInstance& component, World& world, EntityId entity);
};

// An asset's stable id as a scene field. Saved as the 16-hex id; load also
// accepts the cook-stamped {"id","path"} ref object -- the id inside the
// stamp is the value, no registry resolution involved -- and reads a bare
// asset:// path (a cook that had no id map) as the invalid id rather than
// failing the entity.
template <>
struct SceneFieldCodec<AssetId>
{
    static bool Save(IWriteArchive&, std::string_view, AssetId,
                     SceneSerializationContext&);
    static bool Load(IReadArchive&, std::string_view, AssetId&,
                     SceneSerializationContext&);
};

template <>
struct TypeSchema<SceneInstance>
{
    static constexpr std::string_view Name = "scene_instance";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('S', 'N', 'I', 'N');

    static auto Fields()
    {
        return std::tuple{
            MakeField("source", &SceneInstance::Source),
            MakeField("id", &SceneInstance::Id),
        };
    }
};
