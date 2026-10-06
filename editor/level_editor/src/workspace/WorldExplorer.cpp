#include "workspace/WorldExplorer.h"

#include "document/EntityLabel.h"
#include "document/commands/ValueCommand.h"
#include "selection/commands/SelectCommand.h"
#include "text/TextFilterMatch.h"
#include "workspace/EditorWorkspace.h"

#include <world/identity/PersistentIdComponent.h>

#include <algorithm>
#include <charconv>
#include <unordered_map>

namespace
{
std::uint64_t PersistentOf(const EditorScene& scene, EntityId entity)
{
    const auto* id = scene.GetRegistry().Components.TryGet<PersistentIdComponent>(entity);
    return id != nullptr ? id->Id.Value : 0;
}
} // namespace

std::string WorldExplorer::KeyOf(EntityId entity)
{
    return std::to_string(entity.Index) + ":" + std::to_string(entity.Generation);
}

bool WorldExplorer::IsOpen(EntityId entity) const
{
    const EditorDocument& document = Workspace.World.FocusDocument();
    const std::uint64_t pid = PersistentOf(document.GetScene(), entity);
    return document.IsSceneInstanceRoot(entity) ? OpenedPlacements.contains(pid) : !Collapsed.contains(pid);
}

void WorldExplorer::SetOpen(EntityId entity, bool open)
{
    const EditorDocument& document = Workspace.World.FocusDocument();
    const std::uint64_t pid = PersistentOf(document.GetScene(), entity);
    if (document.IsSceneInstanceRoot(entity))
    {
        if (open)
            OpenedPlacements.insert(pid);
        else
            OpenedPlacements.erase(pid);
    }
    else if (open)
        Collapsed.erase(pid);
    else
        Collapsed.insert(pid);
}

std::vector<WorldExplorer::Row> WorldExplorer::Rows(std::string_view filter)
{
    const EditorDocument& document = Workspace.World.FocusDocument();
    const EditorScene& scene = document.GetScene();
    const std::span<const EntityId> all = scene.GetAllEntities();

    std::unordered_map<EntityId, std::vector<EntityId>, EntityIdHash> children;
    std::vector<EntityId> roots;
    std::unordered_set<EntityId, EntityIdHash> tracked(all.begin(), all.end());
    for (const EntityId entity : all)
    {
        const EntityId parent = scene.GetParent(entity);
        // An untracked parent has no row: its child shows at the root rather
        // than vanishing.
        if (parent.IsValid() && tracked.contains(parent))
            children[parent].push_back(entity);
        else
            roots.push_back(entity);
    }

    // A selection made elsewhere is revealed: its ancestors open once.
    const SelectableRef primary = Workspace.Selection.GetPrimarySelection();
    if (primary.IsEntity() && primary.Entity != Revealed && scene.HasEntity(primary.Entity))
    {
        Revealed = primary.Entity;
        for (EntityId ancestor = scene.GetParent(primary.Entity); ancestor.IsValid(); ancestor = scene.GetParent(ancestor))
            SetOpen(ancestor, true);
    }

    // With a filter, a branch stays when anything in it matches, and shows open.
    std::unordered_map<EntityId, bool, EntityIdHash> matches;
    const auto match = [&](auto&& self, EntityId entity) -> bool {
        bool any = TextFilterMatch(EntityLabel(document, entity), filter);
        for (const EntityId child : children[entity])
            any = self(self, child) || any;
        matches[entity] = any;
        return any;
    };
    const bool filtering = !filter.empty();
    if (filtering)
        for (const EntityId root : roots)
            (void)match(match, root);

    std::vector<Row> rows;
    const auto walk = [&](auto&& self, EntityId entity, int depth) -> void {
        if (filtering && !matches[entity])
            return;
        const std::vector<EntityId>& below = children[entity];
        const bool open = filtering || IsOpen(entity);
        rows.push_back({ entity, depth,
                         below.empty() ? ExplorerBranch::Leaf : open ? ExplorerBranch::Open : ExplorerBranch::Closed });
        if (open)
            for (const EntityId child : below)
                self(self, child, depth + 1);
    };
    for (const EntityId root : roots)
        walk(walk, root, 0);
    return rows;
}

ExplorerPage WorldExplorer::Explore(const ExplorerQuery& query)
{
    const EditorDocument& document = Workspace.World.FocusDocument();
    const EditorScene& scene = document.GetScene();
    const std::vector<Row> rows = Rows(query.Filter);

    std::unordered_set<EntityId, EntityIdHash> selected;
    for (const SelectableRef& ref : Workspace.Selection.GetSelection())
        if (ref.IsEntity())
            selected.insert(ref.Entity);

    ExplorerPage page;
    page.Total = rows.size();
    page.Actions = { "entity.create" };
    page.Empty = scene.GetAllEntities().empty() ? "The scene is empty." : "No entity's name holds that.";
    Shown.clear();
    for (std::size_t i = 0; i < rows.size(); ++i)
    {
        Shown.push_back(rows[i].Entity);
        if (i < query.First || page.Items.size() >= query.Count)
            continue;
        const EntityId entity = rows[i].Entity;
        page.Items.push_back({ .Key = KeyOf(entity),
                               .Label = EntityLabel(document, entity),
                               .Depth = rows[i].Depth,
                               .Branch = rows[i].Branch,
                               .Current = selected.contains(entity),
                               .Marks = { scene.IsEntityVisible(entity) ? IconId::Eye : IconId::EyeOff,
                                          scene.IsEntityLocked(entity) ? IconId::Lock : IconId::Unlock } });
    }
    return page;
}

EntityId WorldExplorer::Resolve(std::string_view key) const
{
    const std::size_t colon = key.find(':');
    std::uint32_t index = 0;
    std::uint32_t generation = 0;
    if (colon == std::string_view::npos
        || std::from_chars(key.data(), key.data() + colon, index).ec != std::errc{}
        || std::from_chars(key.data() + colon + 1, key.data() + key.size(), generation).ec != std::errc{})
        return {};
    for (const EntityId entity : Workspace.World.FocusDocument().GetScene().GetAllEntities())
        if (entity.Index == index && entity.Generation == generation)
            return entity;
    return {};
}

void WorldExplorer::Select(EntityId entity, ExplorerChoice choice)
{
    const RegistryId registry = Workspace.World.FocusDocument().GetScene().GetRegistry().Id;
    const SelectableRef ref = SelectableRef::EntitySelection(registry, entity);
    SelectionSnapshot next;
    next.Primary = ref;
    if (choice == ExplorerChoice::Extend)
    {
        // Across the rows as shown, from the primary to this one.
        const SelectableRef primary = Workspace.Selection.GetPrimarySelection();
        const auto anchor = std::ranges::find(Shown, primary.IsEntity() ? primary.Entity : EntityId{});
        const auto clicked = std::ranges::find(Shown, entity);
        if (anchor != Shown.end() && clicked != Shown.end())
            for (auto it = std::min(anchor, clicked); it <= std::max(anchor, clicked); ++it)
                next.Items.push_back(SelectableRef::EntitySelection(registry, *it));
        else
            next.Items.push_back(ref);
    }
    else if (choice == ExplorerChoice::Toggle)
    {
        for (const SelectableRef& existing : Workspace.Selection.GetSelection())
            if (!(existing == ref))
                next.Items.push_back(existing);
        if (next.Items.size() == Workspace.Selection.GetSelection().size())
            next.Items.push_back(ref);
        else
            next.Primary = next.Items.empty() ? SelectableRef{} : next.Items.back();
    }
    else
    {
        next.Items.push_back(ref);
    }
    Workspace.Commands.Execute(std::make_unique<SelectCommand>(Workspace.Selection, std::move(next)));
}

void WorldExplorer::Choose(std::string_view key, ExplorerChoice choice, std::size_t mark)
{
    const EntityId entity = Resolve(key);
    if (!entity.IsValid())
        return;
    EditorDocument& document = Workspace.World.FocusDocument();
    EditorScene& scene = document.GetScene();
    switch (choice)
    {
    case ExplorerChoice::Open:
        SetOpen(entity, !IsOpen(entity));
        return;
    case ExplorerChoice::Reveal:
        for (EntityId ancestor = scene.GetParent(entity); ancestor.IsValid(); ancestor = scene.GetParent(ancestor))
            SetOpen(ancestor, true);
        return;
    case ExplorerChoice::Mark:
    {
        const bool visibility = mark == 0;
        const bool now = visibility ? scene.IsEntityVisible(entity) : scene.IsEntityLocked(entity);
        Workspace.Commands.Execute(std::make_unique<ValueCommand<bool>>(
            now, !now,
            [&scene, entity, visibility](const bool& value) {
                if (visibility)
                    scene.SetEntityVisible(entity, value);
                else
                    scene.SetEntityLocked(entity, value);
            },
            document));
        return;
    }
    case ExplorerChoice::Pick:
    case ExplorerChoice::Commit:
    case ExplorerChoice::Extend:
    case ExplorerChoice::Toggle:
    default:
        Select(entity, choice);
        return;
    }
}
