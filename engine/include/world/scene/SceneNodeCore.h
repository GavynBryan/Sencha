#pragma once

#include <cstdint>

//=============================================================================
// SceneNodeCore
//
// Plain data struct holding dimension-agnostic identity for any SceneNode.
// Lives as a by-value member of SceneNode<TTransform>, not a base class —
// SceneNode is not polymorphic and never will be.
//
// Keep this aggressively minimal: fields that 2D and 3D genuinely share, and
// nothing else. Anything dimension-specific belongs on the templated node,
// anything gameplay-specific belongs on the composing gameplay class.
//=============================================================================
struct SceneNodeCore
{
	uint32_t Id = 0;
	uint32_t Flags = 0;
};
