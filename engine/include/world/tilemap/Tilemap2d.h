#pragma once

#include <core/batch/DataBatchKey.h>
#include <core/raii/DataBatchHandle.h>
#include <math/geometry/2d/Transform2d.h>
#include <world/transform/TransformHierarchyRegistration.h>
#include <world/World2d.h>
#include <cstdint>
#include <vector>

//=============================================================================
// Tilemap2d
//
// A gameplay object that owns a transform in World2d and participates in the
// transform hierarchy WITHOUT being a SceneNode. Proof that the transform
// system is a flat service any gameplay type can opt into by holding a
// DataBatchHandle + TransformHierarchyRegistration — inheritance from
// SceneNode is not required.
//
// The tilemap owns exactly one transform (the grid origin). Individual tiles
// are not transforms; they are cells in the grid addressed by (col, row) and
// resolved against the tilemap's world transform at draw/query time. Putting
// a transform per tile would blow up the propagation set for no benefit — all
// tiles move rigidly with the tilemap.
//=============================================================================
class Tilemap2d
{
public:
	Tilemap2d(
		World2d& world,
		const Transform2f& origin,
		uint32_t gridWidth,
		uint32_t gridHeight,
		float tileSize)
		: Handle(world.Transforms.Emplace(origin))
		, Registration(world.TransformHierarchy, Handle.GetToken())
		, GridWidth(gridWidth)
		, GridHeight(gridHeight)
		, TileSize(tileSize)
		, Tiles(static_cast<size_t>(gridWidth) * gridHeight, 0)
	{
	}

	Tilemap2d(Tilemap2d&&) noexcept = default;
	Tilemap2d& operator=(Tilemap2d&&) noexcept = default;
	Tilemap2d(const Tilemap2d&) = delete;
	Tilemap2d& operator=(const Tilemap2d&) = delete;

	// -- Transform participation -------------------------------------------

	DataBatchKey TransformKey() const { return Handle.GetToken(); }

	void SetTransformParent(DataBatchKey parentKey)
	{
		Registration.GetService()->SetParent(Handle.GetToken(), parentKey);
	}

	void ClearTransformParent()
	{
		Registration.GetService()->ClearParent(Handle.GetToken());
	}

	// -- Grid data ---------------------------------------------------------

	uint32_t Width() const { return GridWidth; }
	uint32_t Height() const { return GridHeight; }
	float GetTileSize() const { return TileSize; }

	uint32_t GetTile(uint32_t col, uint32_t row) const
	{
		return Tiles[Index(col, row)];
	}

	void SetTile(uint32_t col, uint32_t row, uint32_t tileId)
	{
		Tiles[Index(col, row)] = tileId;
	}

private:
	size_t Index(uint32_t col, uint32_t row) const
	{
		return static_cast<size_t>(row) * GridWidth + col;
	}

	DataBatchHandle<Transform2f> Handle;
	TransformHierarchyRegistration Registration;
	uint32_t GridWidth = 0;
	uint32_t GridHeight = 0;
	float TileSize = 0.0f;
	std::vector<uint32_t> Tiles;
};
