#include "TransitionConnect.h"

#include "EditorDocument.h"
#include "WorldDocument.h"
#include "commands/LinkPortalCommand.h"

#include "commands/CommandStack.h"

#include <utility>

TransitionId ConnectZones(WorldDocument& world, ZoneId from, ZoneId to, bool oneWay,
                          EntityId portal, CommandStack& commands)
{
    if (!world.IsWorld() || from == to)
        return TransitionId{};
    const auto zoneExists = [&](ZoneId zone)
    {
        for (const ZoneHeader& header : world.Manifest().Zones)
            if (header.Id == zone)
                return true;
        return false;
    };
    if (!zoneExists(from) || !zoneExists(to))
        return TransitionId{};

    const TransitionId forward =
        world.AddTransition(from, to, TransitionTopology::Doorway, oneWay, 0);
    if (!oneWay)
        (void)world.AddTransition(to, from, TransitionTopology::Doorway, false, 0);

    if (portal.IsValid() && world.FocusZone() == from
        && world.FocusDocument().GetScene().IsPortal(portal))
    {
        if (auto link = MakeLinkPortalCommand(world.FocusDocument(), portal, forward))
        {
            commands.Execute(std::move(link));
            world.Revalidate();
        }
    }
    return forward;
}
