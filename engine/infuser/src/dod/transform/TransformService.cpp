#include <dod/transform/TransformService.h>
#include <dod/transform/TransformHierarchyService.h>
#include <dod/transform/TransformPropagationSystem.h>
#include <batch/DataBatch.h>
#include <math/Transform2.h>
#include <math/Transform3.h>
#include <service/ServiceHost.h>
#include <system/SystemHost.h>
#include <system/SystemPhase.h>

namespace TransformService {
    void SetupContiguousTransformBatches2D(ServiceHost& serviceHost)
    {
        serviceHost.AddTaggedService<DataBatch<Transform2f>, Tags::LocalTransformTag>();
        serviceHost.AddTaggedService<DataBatch<Transform2f>, Tags::WorldTransformTag>();
    }

    void SetupContiguousTransformBatches3D(ServiceHost& serviceHost)
    {
        serviceHost.AddTaggedService<DataBatch<Transform3f>, Tags::LocalTransformTag>();
        serviceHost.AddTaggedService<DataBatch<Transform3f>, Tags::WorldTransformTag>();
    }

    void SetupTransformPropagationStack2D(ServiceHost& serviceHost, SystemHost& systemHost)
    {
        serviceHost.AddService<TransformHierarchyService<Tags::Transform2DTag>>();
        systemHost.AddSystem<TransformPropagationSystem<
            Transform2f,
            Tags::Transform2DTag>>(SystemPhase::PostUpdate);
    }

	void SetupTransformPropagationStack3D(ServiceHost& serviceHost, SystemHost& systemHost)
    {
        serviceHost.AddService<TransformPropagationSystem<
            Transform3f,
            Tags::Transform3DTag>>();
    }
}