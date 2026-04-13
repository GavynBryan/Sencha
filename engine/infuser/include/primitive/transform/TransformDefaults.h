#pragma once

class ServiceHost;
class SystemHost;

//=============================================================================
// TransformDefaults
//
// Empty structs used as ServiceHost tags for transform roles and domains.
//=============================================================================
namespace TransformDefaults {

	namespace Tags {
		struct Transform2DTag {};
		struct Transform3DTag {};

		struct WorldTransformTag {};
		struct LocalTransformTag {};
	}

	void SetupContiguousTransformBatches2D(ServiceHost& serviceHost);
	void SetupContiguousTransformBatches3D(ServiceHost& serviceHost);

	void SetupTransformPropagationStack2D(ServiceHost& serviceHost, SystemHost& systemHost);
	void SetupTransformPropagationStack3D(ServiceHost& serviceHost, SystemHost& systemHost);
}
