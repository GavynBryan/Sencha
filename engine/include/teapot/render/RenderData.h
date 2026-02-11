#pragma once

#include <teapot/math/Transform.h>

//=============================================================================
// RenderData2D
//
// Plain data for a 2D renderable object, stored in a DataBatch<RenderData2D>
// for cache-friendly iteration. The render system reads these values
// contiguously each frame and submits them to the IGraphicsAPI.
//
// This replaces the virtual-dispatch model (IRenderable::Render) with a
// data-oriented model: the system owns the logic, the batch owns the data.
//=============================================================================
struct RenderData2D
{
	Transform2D Transform{};
	int RenderOrder = 0;
	bool bIsVisible = true;
};

//=============================================================================
// RenderData3D
//
// Plain data for a 3D renderable object. Same design as RenderData2D but
// with three-dimensional position, scale, and rotation (Euler angles).
//=============================================================================
struct RenderData3D
{
	Transform3D Transform{};
	int RenderOrder = 0;
	bool bIsVisible = true;
};
