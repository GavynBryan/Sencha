#pragma once

#include "commands/ICommand.h"
#include "document/WorldDocument.h"

class SetZoneBoundsCommand final : public ICommand
{
public:
    SetZoneBoundsCommand(WorldDocument& world, ZoneId zone,
                         Aabb3d before, bool beforeOverride,
                         Aabb3d after, bool afterOverride)
        : World(world), Zone(zone), Before(before), After(after)
        , BeforeOverride(beforeOverride), AfterOverride(afterOverride) {}

    void Execute() override { (void)World.SetZoneBounds(Zone, After, AfterOverride); }
    void Undo() override { (void)World.SetZoneBounds(Zone, Before, BeforeOverride); }

private:
    WorldDocument& World;
    ZoneId Zone;
    Aabb3d Before;
    Aabb3d After;
    bool BeforeOverride = false;
    bool AfterOverride = true;
};
