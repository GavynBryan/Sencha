#include <render/PresentationLayers.h>

#include <zone/WorldPartitionManifest.h>

#include <algorithm>
#include <cassert>

void PresentationLayerStack::Push(PresentationLayerEntry entry)
{
    assert(Count < Entries.size());
    Entries[Count++] = entry;
}

PresentationLayerStack PresentationLayersAt(const PresentationSettingsLayer* game,
                                            const WorldPartitionManifest* world, ZoneId zone,
                                            const PresentationSettingsLayer* scene)
{
    PresentationLayerStack stack;
    if (game != nullptr)
        stack.Push({ .Level = PresentationLayerLevel::Game, .Name = {}, .Settings = game });
    if (world != nullptr)
    {
        stack.Push({ .Level = PresentationLayerLevel::World, .Name = world->Name, .Settings = &world->Presentation });
        const auto header = std::ranges::find(world->Zones, zone, &ZoneHeader::Id);
        if (header != world->Zones.end())
        {
            const auto graph = std::ranges::find(world->Graphs, header->Graph, &GraphRecord::Id);
            if (graph != world->Graphs.end())
                stack.Push({ .Level = PresentationLayerLevel::Graph, .Name = graph->Name,
                             .Settings = &graph->Presentation });
        }
    }
    if (scene != nullptr)
        stack.Push({ .Level = PresentationLayerLevel::Scene, .Name = {}, .Settings = scene });
    return stack;
}

PresentationSettings ResolvePresentationSettings(std::span<const PresentationLayerEntry> layers)
{
    PresentationSettings settings;
    for (const PresentationLayerEntry& layer : layers)
        ApplyPresentationSettingsLayer(settings, *layer.Settings);
    return settings;
}
