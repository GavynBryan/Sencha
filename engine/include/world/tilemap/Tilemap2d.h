#pragma once

#include <core/batch/DataBatchKey.h>
#include <core/handle/DataBatchHandle.h>
#include <math/geometry/2d/Transform2d.h>
#include <math/spatial/Grid2d.h>
#include <world/transform/TransformSpace.h>
#include <world/transform/TransformHierarchyRegistration.h>
#include <cstdint>

//=============================================================================
// Tilemap2d
//
// Thin struct that owns a transform slot in a 2D TransformSpace and wraps
// a Grid2d<uint32_t> for tile data. No render or visibility state lives here;
// rendering decisions belong to TilemapRenderState (a separate DataBatch).
//
// Transform participation is opt-in composition: holding a DataBatchHandle
// and a TransformHierarchyRegistration is sufficient — no base class required.
//
// Individual tiles are NOT transforms. All cells move rigidly with the map's
// single world transform, resolved at draw time via TransformKey().
//=============================================================================
struct Tilemap2d
{
    Tilemap2d(
        TransformSpace<Transform2f>& domain,
        const Transform2f& origin,
        uint32_t gridWidth,
        uint32_t gridHeight,
        float tileSize)
        : Handle(domain.Transforms.Emplace(origin))
        , Registration(domain.Hierarchy, Handle.GetToken())
        , TileSize(tileSize)
        , Grid(gridWidth, gridHeight, 0u)
    {
    }

    Tilemap2d(Tilemap2d&&) noexcept = default;
    Tilemap2d& operator=(Tilemap2d&&) noexcept = default;
    Tilemap2d(const Tilemap2d&) = delete;
    Tilemap2d& operator=(const Tilemap2d&) = delete;

    // -- Transform participation ----------------------------------------------

    DataBatchKey TransformKey() const { return Handle.GetToken(); }

    void SetTransformParent(DataBatchKey parentKey)
    {
        Registration.GetService()->SetParent(Handle.GetToken(), parentKey);
    }

    void ClearTransformParent()
    {
        Registration.GetService()->ClearParent(Handle.GetToken());
    }

    // -- Grid access ----------------------------------------------------------

    uint32_t Width()        const { return Grid.GetWidth(); }
    uint32_t Height()       const { return Grid.GetHeight(); }
    float    GetTileSize()  const { return TileSize; }

    uint32_t GetTile(uint32_t col, uint32_t row) const { return Grid.Get(col, row); }
    void     SetTile(uint32_t col, uint32_t row, uint32_t tileId) { Grid.Set(col, row, tileId); }

    // -- Data members (public for DataBatch move-construction) ----------------

    DataBatchHandle<Transform2f>   Handle;
    TransformHierarchyRegistration Registration;
    float                          TileSize = 1.0f;
    Grid2d<uint32_t>               Grid;
};
