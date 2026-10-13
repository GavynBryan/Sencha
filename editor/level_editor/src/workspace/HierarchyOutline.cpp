#include "workspace/HierarchyOutline.h"

#include "workspace/EditorWorkspace.h"
#include "workspace/LevelLocation.h"
#include "workspace/PartitionOutline.h"
#include "workspace/WorldExplorer.h"

#include <SDL3/SDL_keycode.h>

#include <algorithm>

ExplorerRow HierarchyOutline::Row()
{
    return { .Id = std::string(kId),
             .Title = "Hierarchy",
             .Chords = { { SDLK_TAB, {} } },
             .Explore = [this](const ExplorerQuery& query) { return Explore(query); },
             .Choose = [this](std::string_view key, ExplorerChoice choice, std::size_t mark) {
                 Choose(key, choice, mark);
             },
             .Shape = ExplorerShape::Outline };
}

ExplorerPage HierarchyOutline::Explore(const ExplorerQuery& query)
{
    WorldDocument& world = Workspace.World;
    std::vector<OutlineNode> nodes = Partition.OutlineNodes();
    // The entities sit under what is being edited.
    const std::string anchor = world.IsWorldSceneFocused() ? LevelLocationKey(WorldScenePlace{})
                                                           : LevelLocationKey(world.FocusZone());
    const auto under = std::ranges::find(nodes, anchor, &OutlineNode::Key);
    const int depth = under != nodes.end() ? under->Depth + 1 : 0;
    std::vector<OutlineNode> entities = Entities.OutlineNodes(depth);
    const bool sceneEmpty = entities.empty();
    nodes.insert(under != nodes.end() ? under + 1 : nodes.end(), std::make_move_iterator(entities.begin()),
                 std::make_move_iterator(entities.end()));

    ExplorerPage page = Outline.Explore(nodes, query);
    page.Actions = { "entity.create" };
    if (world.IsWorld())
    {
        page.Actions.push_back("world.add_graph");
        page.Empty = "Nothing's name holds that.";
    }
    else
        page.Empty = sceneEmpty && query.Filter.empty() ? "The scene is empty." : "No entity's name holds that.";
    return page;
}

void HierarchyOutline::Choose(std::string_view key, ExplorerChoice choice, std::size_t mark)
{
    if (choice == ExplorerChoice::Open)
    {
        Outline.Flip(key);
        return;
    }
    if (choice == ExplorerChoice::Reveal)
        Outline.Reveal(std::string(key));
    const std::optional<LevelLocation> location = ParseLevelLocation(key);
    if (!location.has_value())
        return;
    if (const PersistentEntityId* entity = std::get_if<PersistentEntityId>(&*location))
        Entities.Choose(*entity, choice, mark);
    else
        Partition.Choose(*location, choice);
}
