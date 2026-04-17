#pragma once

#include <core/batch/DataBatchKey.h>
#include <graphics/vulkan/VulkanDescriptorCache.h>
#include <cstdint>

//=============================================================================
// TilemapRenderState
//
// Pure data descriptor that binds a Tilemap2d map to a rendered layer. Lives
// in a DataBatch<TilemapRenderState> that is entirely separate from the
// DataBatch<Tilemap2d> holding tile data — render state and game state are
// never co-located in the same array.
//
// MapKey       — key into DataBatch<Tilemap2d>; provides grid dimensions,
//                tile size, and per-cell tile IDs.
// TransformKey — key into the world Transform batch (from TransformStore);
//                TilemapRenderFeature reads the world matrix from here each
//                frame without reaching into the Tilemap2d.
// TilesetTexture  — bindless slot registered with VulkanDescriptorCache.
// TilesetColumns / TilesetRows — grid layout of the tileset spritesheet;
//                used to compute per-tileId UV rects at draw time.
// LayerZIndex  — ascending sort order; lower values draw first (behind).
//=============================================================================
struct TilemapRenderState
{
    DataBatchKey       MapKey;
    DataBatchKey       TransformKey;
    BindlessImageIndex TilesetTexture;
    uint32_t           TilesetColumns = 1;
    uint32_t           TilesetRows    = 1;
    int32_t            LayerZIndex    = 0;
};
