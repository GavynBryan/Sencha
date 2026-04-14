#pragma once

//=============================================================================
// TransformServiceTags
//
// Internal ServiceHost tags for transform domains and local/world batch roles.
// Gameplay should use World and TransformStore services instead of these tags.
//=============================================================================
namespace TransformServiceTags {

	struct Transform2DTag {};
	struct Transform3DTag {};

	struct WorldTransformTag {};
	struct LocalTransformTag {};
}
