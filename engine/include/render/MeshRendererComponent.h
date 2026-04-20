#pragma once

#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <world/entity/EntityId.h>
#include <render/Material.h>
#include <render/MeshTypes.h>
#include <world/SparseSetStore.h>

#include <cstdint>
#include <string_view>
#include <tuple>

//=============================================================================
// MeshRendererComponent
//
// ECS component that pairs an entity with a mesh and material. LayerMask and
// SubmeshMask are bitmasks: a cleared bit skips the corresponding layer or
// submesh during extraction.
//=============================================================================
struct MeshRendererComponent
{
    MeshHandle Mesh;
    MaterialHandle Material;
    bool Visible = true;
    uint32_t LayerMask = 0xFFFFFFFFu;
    uint32_t SubmeshMask = 0xFFFFFFFFu;
};

using MeshRendererStore = SparseSetStore<MeshRendererComponent>;

template <>
struct TypeSchema<MeshRendererComponent>
{
    static constexpr std::string_view Name = "MeshRenderer";

    static auto Fields()
    {
        return std::tuple{
            MakeField("mesh", &MeshRendererComponent::Mesh),
            MakeField("material", &MeshRendererComponent::Material),
            MakeField("visible", &MeshRendererComponent::Visible),
            MakeField("layer_mask", &MeshRendererComponent::LayerMask),
            MakeField("submesh_mask", &MeshRendererComponent::SubmeshMask),
        };
    }
};
