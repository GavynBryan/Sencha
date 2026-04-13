#include <batch/DataBatch.h>
#include <math/Transform2.h>
#include <math/Transform3.h>
#include <primitive/transform/core/TransformDefaults.h>
#include <primitive/transform/hierarchy/TransformHierarchyService.h>
#include <primitive/transform/hierarchy/TransformPropagationSystem.h>
#include <service/ServiceHost.h>
#include <service/ServiceProvider.h>
#include <system/SystemHost.h>
#include <system/SystemPhase.h>

namespace TransformDefaults {
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
        ServiceProvider provider(serviceHost);
        systemHost.AddSystem<TransformPropagationSystem<
            Transform2f,
            Tags::Transform2DTag>>(SystemPhase::PostUpdate, provider);
    }

	void SetupTransformPropagationStack3D(ServiceHost& serviceHost, SystemHost& systemHost)
    {
        serviceHost.AddService<TransformHierarchyService<Tags::Transform3DTag>>();
        ServiceProvider provider(serviceHost);
        systemHost.AddSystem<TransformPropagationSystem<
            Transform3f,
            Tags::Transform3DTag>>(SystemPhase::PostUpdate, provider);
    }
}
