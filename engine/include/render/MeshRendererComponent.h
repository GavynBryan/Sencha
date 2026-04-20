#pragma once

#include <world/entity/EntityId.h>
#include <render/Material.h>
#include <render/MeshTypes.h>

#include <cstdint>

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
