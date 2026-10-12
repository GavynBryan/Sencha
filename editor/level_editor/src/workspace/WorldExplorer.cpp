#include "workspace/WorldExplorer.h"

#include "document/EntityLabel.h"
#include "document/commands/EntityFlagCommand.h"
#include "selection/SelectionFold.h"
#include "selection/commands/SelectCommand.h"
#include "workspace/EditorWorkspace.h"
#include "workspace/LevelLocation.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

std::vector<OutlineNode> WorldExplorer::OutlineNodes(int depth)
{
    const EditorDocument& document = Workspace.World.FocusDocument();
    const EditorScene& scene = document.GetScene();
    const std::span<const EntityId> all = scene.GetAllEntities();

    std::unordered_map<EntityId, std::vector<EntityId>, EntityIdHash> children;
    std::vector<EntityId> roots;
    const std::unordered_set<EntityId, EntityIdHash> tracked(all.begin(), all.end());
    for (const EntityId entity : all)
    {
        const EntityId parent = scene.GetParent(entity);
        // An untracked parent has no node: its child shows at the root rather
        // than vanishing.
        if (parent.IsValid() && tracked.contains(parent))
            children[parent].push_back(entity);
        else
            roots.push_back(entity);
    }

    std::unordered_set<EntityId, EntityIdHash> selected;
    for (const SelectableRef& ref : Workspace.Selection.GetSelection())
        if (ref.IsEntity())
            selected.insert(ref.Entity);

    std::vector<OutlineNode> nodes;
    Listed.clear();
    const auto walk = [&](auto&& self, EntityId entity, int at) -> void {
        const PersistentEntityId id = scene.PersistentIdOf(entity);
        Listed.push_back(id);
        nodes.push_back({ .Key = LevelLocationKey(id),
                          .Label = EntityLabel(document, entity),
                          .Depth = at,
                          .Current = selected.contains(entity),
                          .Marks = { scene.IsEntityVisible(entity) ? IconId::Eye : IconId::EyeOff,
                                     scene.IsEntityLocked(entity) ? IconId::Lock : IconId::Unlock },
                          .StartsClosed = document.IsSceneInstanceRoot(entity) });
        for (const EntityId child : children[entity])
            self(self, child, at + 1);
    };
    for (const EntityId root : roots)
        walk(walk, root, depth);
    return nodes;
}

void WorldExplorer::Choose(PersistentEntityId id, ExplorerChoice choice, std::size_t mark)
{
    EditorDocument& document = Workspace.World.FocusDocument();
    const EditorScene& scene = document.GetScene();
    const EntityId entity = scene.EntityWithPersistentId(id);
    if (!entity.IsValid())
        return;
    if (choice == ExplorerChoice::Mark)
    {
        const EntityFlag flag = mark == 0 ? EntityFlag::Visible : EntityFlag::Locked;
        const EntityId one[] = { entity };
        if (std::unique_ptr<ICommand> step = MakeSetEntityFlagCommand(one, flag, !EntityFlagOf(scene, entity, flag), document))
            Workspace.Commands.Execute(std::move(step));
        return;
    }
    if (choice == ExplorerChoice::Open || choice == ExplorerChoice::Reveal)
        return;

    const RegistryId registry = scene.GetRegistry().Id;
    const SelectableRef ref = SelectableRef::EntitySelection(registry, entity);
    std::vector<SelectableRef> gathered{ ref };
    SelectionFold::Op op = choice == ExplorerChoice::Toggle ? SelectionFold::Op::Toggle : SelectionFold::Op::Replace;
    if (choice == ExplorerChoice::Extend)
    {
        // Across the outline as listed, from the primary to this one.
        const SelectableRef primary = Workspace.Selection.GetPrimarySelection();
        const auto anchor = std::ranges::find(Listed, primary.IsEntity() ? scene.PersistentIdOf(primary.Entity)
                                                                          : PersistentEntityId{});
        const auto clicked = std::ranges::find(Listed, id);
        if (anchor != Listed.end() && clicked != Listed.end())
        {
            gathered.clear();
            for (auto it = std::min(anchor, clicked); it <= std::max(anchor, clicked); ++it)
                if (const EntityId between = scene.EntityWithPersistentId(*it); between.IsValid())
                    gathered.push_back(SelectableRef::EntitySelection(registry, between));
        }
    }
    SelectionSnapshot next = SelectionFold::Apply(Workspace.Selection.GetSnapshot(), gathered, op);
    if (op == SelectionFold::Op::Replace)
        next.Primary = ref;
    Workspace.Commands.Execute(std::make_unique<SelectCommand>(Workspace.Selection, std::move(next)));
}
