#pragma once

#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <render/Material.h>
#include <render/static_mesh/StaticMeshHandle.h>
#include <world/entity/EntityId.h>

#include <cstdint>
#include <string_view>
#include <tuple>

//=============================================================================
// StaticMeshComponent
//
// ECS component that pairs an entity with a mesh and material. LayerMask and
// SectionMask are bitmasks: a cleared bit skips the corresponding layer or
// section during extraction.
//=============================================================================
struct StaticMeshComponent
{
    StaticMeshHandle Mesh;
    MaterialHandle Material;
    bool Visible = true;
    uint32_t LayerMask = 0xFFFFFFFFu;
    uint32_t SectionMask = 0xFFFFFFFFu;
};

template <>
struct TypeSchema<StaticMeshComponent>
{
    static constexpr std::string_view Name = "StaticMesh";

    static auto Fields()
    {
        return std::tuple{
            MakeField("mesh", &StaticMeshComponent::Mesh),
            MakeField("material", &StaticMeshComponent::Material),
            MakeField("visible", &StaticMeshComponent::Visible),
            MakeField("layer_mask", &StaticMeshComponent::LayerMask),
            MakeField("section_mask", &StaticMeshComponent::SectionMask),
        };
    }
};
