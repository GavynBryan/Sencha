#include <core/service/ServiceHost.h>
#include <core/system/SystemHost.h>
#include <world/WorldSetup.h>

int main()
{
    ServiceHost services;
    SystemHost systems;

    WorldSetup::Setup2D(services, systems);

    return 0;
}
