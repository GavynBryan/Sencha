#include <core/batch/DataBatch.h>
#include <math/geometry/2d/Transform2d.h>
#include <math/geometry/3d/Transform3d.h>
#include <leaves/transform/core/TransformDefaults.h>
#include <leaves/transform/hierarchy/TransformHierarchyService.h>
#include <leaves/transform/hierarchy/TransformPropagationOrderService.h>
#include <leaves/transform/hierarchy/TransformPropagationSystem.h>
#include <core/service/ServiceHost.h>
#include <core/service/ServiceProvider.h>
#include <core/system/SystemHost.h>
#include <core/system/SystemPhase.h>

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
        serviceHost.AddService<TransformPropagationOrderService<Tags::Transform2DTag>>();
        ServiceProvider provider(serviceHost);
        systemHost.AddSystem<TransformPropagationSystem<
            Transform2f,
            Tags::Transform2DTag>>(SystemPhase::PostUpdate, provider);
    }

	void SetupTransformPropagationStack3D(ServiceHost& serviceHost, SystemHost& systemHost)
    {
        serviceHost.AddService<TransformHierarchyService<Tags::Transform3DTag>>();
        serviceHost.AddService<TransformPropagationOrderService<Tags::Transform3DTag>>();
        ServiceProvider provider(serviceHost);
        systemHost.AddSystem<TransformPropagationSystem<
            Transform3f,
            Tags::Transform3DTag>>(SystemPhase::PostUpdate, provider);
    }
}
