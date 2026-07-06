#include "TransitionConnect.h"

#include "WorldDocument.h"

#include <utility>

std::string JoinTagList(const std::vector<std::string>& tags)
{
    std::string joined;
    for (const std::string& tag : tags)
    {
        if (!joined.empty())
            joined += ", ";
        joined += tag;
    }
    return joined;
}

std::vector<std::string> SplitTagList(std::string_view text)
{
    std::vector<std::string> tags;
    std::string current;
    for (const char c : text)
    {
        if (c == ',' || c == ' ')
        {
            if (!current.empty())
                tags.push_back(std::move(current));
            current.clear();
            continue;
        }
        current += c;
    }
    if (!current.empty())
        tags.push_back(std::move(current));
    return tags;
}

std::string TransitionDisplayName(const WorldPartitionManifest& manifest,
                                  const TransitionRecord& record)
{
    if (!record.Name.empty())
        return record.Name;
    const auto zoneName = [&](ZoneId zone) -> std::string
    {
        for (const ZoneHeader& header : manifest.Zones)
            if (header.Id == zone)
                return header.Name;
        return ZoneIdToString(zone);
    };
    return zoneName(record.From) + " -> " + zoneName(record.To);
}

TransitionId ConnectZones(WorldDocument& world, ZoneId from, ZoneId to, bool oneWay)
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
    return forward;
}
