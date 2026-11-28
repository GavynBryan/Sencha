#include <line/ContactLineRegistration.h>

#include <line/ContactLineSystem.h>
#include <world/relocation/RelocationRules.h>

namespace
{
    void DrawLinesHome(void*, World& world, std::span<const EntityId> group, const RelocationFrame&)
    {
        RetractContactLinesOf(world, group);
    }
}

void RegisterContactLineRelocation(World& world)
{
    if (world.IsRegistered<ContactLineState>())
        EnsureRelocationRules(world).AddGroupRule(RelocationRules::GroupRule{ .Relocate = &DrawLinesHome });
}
