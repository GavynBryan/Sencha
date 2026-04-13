#include <core/service/ServiceHost.h>
#include <core/system/SystemHost.h>

#include <leaves/transform/core/TransformDefaults.h>


int main()
{
    ServiceHost services;
    SystemHost systems;

    TransformDefaults::SetupContiguousTransformBatches2D(services);
    TransformDefaults::SetupTransformPropagationStack2D(services, systems);

    return 0;
}