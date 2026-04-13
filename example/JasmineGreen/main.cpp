#include <service/ServiceHost.h>
#include <system/SystemHost.h>

#include <primitive/transform/core/TransformDefaults.h>


int main()
{
    ServiceHost services;
    SystemHost systems;

    TransformDefaults::SetupContiguousTransformBatches2D(services);
    TransformDefaults::SetupTransformPropagationStack2D(services, systems);

    return 0;
}