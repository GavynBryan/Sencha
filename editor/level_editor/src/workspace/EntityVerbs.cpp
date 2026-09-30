#include "workspace/EntityVerbs.h"

#include "actions/ActionCatalog.h"
#include "actions/ActionOffer.h"
#include "commands/CompositeCommand.h"
#include "document/commands/CreateEntityCommand.h"
#include "document/commands/MoveEntitiesToZoneCommand.h"
#include "document/commands/ReparentEntitiesCommand.h"
#include "document/commands/SceneInstanceCommands.h"
#include "document/commands/SetSceneOriginCommand.h"
#include "document/commands/ValueCommand.h"
#include "selection/commands/SelectCommand.h"
#include "workspace/EditorWorkspace.h"

#include <world/identity/PersistentIdComponent.h>

#include <algorithm>
#include <format>

namespace
{
std::vector<EntityId> SelectedEntities(const EditorWorkspace& workspace)
{
    std::vector<EntityId> entities;
    for (const SelectableRef& ref : workspace.Selection.GetSelection())
        if (ref.IsEntity())
            entities.push_back(ref.Entity);
    return entities;
}

EntityId Primary(const EditorWorkspace& workspace)
{
    const SelectableRef primary = workspace.Selection.GetPrimarySelection();
    return primary.IsEntity() ? primary.Entity : EntityId{};
}
} // namespace

EntityVerbs::EntityVerbs(EditorWorkspace& workspace, std::function<bool(const std::string&)> openSource)
    : Workspace(workspace)
    , OpenSource(std::move(openSource))
{
}

void EntityVerbs::Offer(ActionCatalog& catalog, ActionContext& context)
{
    EditorWorkspace& w = Workspace;
    const auto offer = [&](std::string id, std::string label, std::vector<KeyChord> chords, ActionHandler handler) {
        OfferAction(catalog, context,
                    { .Id = std::move(id), .Label = std::move(label), .Category = "Entity",
                      .DefaultChords = std::move(chords) },
                    std::move(handler));
    };
    const auto document = [&w]() -> EditorDocument& { return w.World.FocusDocument(); };
    const auto anySelected = [&w] { return !SelectedEntities(w).empty(); };
    const auto primaryIsInstance = [&w, document] {
        const EntityId primary = Primary(w);
        return primary.IsValid() && document().IsSceneInstanceRoot(primary);
    };

    offer("entity.create", "New Entity", { { SDLK_N, { .Ctrl = true, .Shift = true } } },
          { .Invoke = [&w, document] {
                EditorDocument& doc = document();
                auto create = MakeCreateEntityCommand(Vec3d::Zero(), doc.GetScene(), doc);
                CreateEntityCommand* created = create.get();
                w.Commands.Execute(std::move(create));
                w.Commands.Execute(std::make_unique<SelectCommand>(
                    w.Selection, SelectableRef::EntitySelection(doc.GetScene().GetRegistry().Id,
                                                                created->GetCreatedEntity())));
            } });

    // Everything selected but the primary goes under it: the primary is the
    // one picked last, as a parent is.
    offer("entity.parent", "Parent to Primary", { { SDLK_P, { .Ctrl = true } } },
          { .Invoke = [&w, document] {
                EditorDocument& doc = document();
                const EntityId parent = Primary(w);
                std::vector<EntityId> children = SelectedEntities(w);
                std::erase(children, parent);
                std::erase_if(children, [&](EntityId child) {
                    return doc.GetScene().IsAncestorOf(child, parent) || doc.IsSceneInstanceMember(child);
                });
                if (auto command = MakeReparentEntitiesCommand(children, parent, ReparentTransformRule::KeepWorld,
                                                               doc.GetScene(), doc))
                    w.Commands.Execute(std::move(command));
            },
            .Enabled = [&w] { return Primary(w).IsValid() && SelectedEntities(w).size() >= 2; } });
    offer("entity.unparent", "Clear Parent", { { SDLK_P, { .Alt = true } } },
          { .Invoke = [&w, document] {
                EditorDocument& doc = document();
                const std::vector<EntityId> entities = SelectedEntities(w);
                if (auto command = MakeReparentEntitiesCommand(entities, EntityId{}, ReparentTransformRule::KeepWorld,
                                                               doc.GetScene(), doc))
                    w.Commands.Execute(std::move(command));
            },
            .Enabled = [&w, document] {
                return std::ranges::any_of(SelectedEntities(w), [&](EntityId entity) {
                    return document().GetScene().GetParent(entity).IsValid();
                });
            } });

    // One step for the whole selection, each entity to the state the primary
    // does not have now.
    const auto flip = [&w, document](bool visibility) {
        EditorDocument& doc = document();
        EditorScene& scene = doc.GetScene();
        const EntityId primary = Primary(w);
        if (!primary.IsValid())
            return;
        const bool to = visibility ? !scene.IsEntityVisible(primary) : !scene.IsEntityLocked(primary);
        std::vector<std::unique_ptr<ICommand>> steps;
        for (const EntityId entity : SelectedEntities(w))
        {
            const bool now = visibility ? scene.IsEntityVisible(entity) : scene.IsEntityLocked(entity);
            if (now == to)
                continue;
            steps.push_back(std::make_unique<ValueCommand<bool>>(
                now, to,
                [&scene, entity, visibility](const bool& value) {
                    if (visibility)
                        scene.SetEntityVisible(entity, value);
                    else
                        scene.SetEntityLocked(entity, value);
                },
                doc));
        }
        if (!steps.empty())
            w.Commands.Execute(std::make_unique<CompositeCommand>(std::move(steps)));
    };
    offer("entity.visible", "Hide or Show", {},
          { .Invoke = [flip] { flip(true); }, .Enabled = anySelected });
    offer("entity.lock", "Lock or Unlock", {},
          { .Invoke = [flip] { flip(false); }, .Enabled = anySelected });

    offer("scene.open_source", "Open Source", {},
          { .Invoke = [this, &w, document] {
                const std::string source = document().SceneInstanceSourceOf(Primary(w));
                if (OpenSource && !OpenSource(source))
                    std::fprintf(stderr, "[editor] source '%s' not found under any content root\n", source.c_str());
            },
            .Enabled = primaryIsInstance });
    offer("scene.break", "Break Scene Instance", {},
          { .Invoke = [&w, document] {
                EditorDocument& doc = document();
                const World& world = doc.GetScene().GetRegistry().Components;
                if (const auto* id = world.TryGet<PersistentIdComponent>(Primary(w)))
                    w.Commands.Execute(std::make_unique<BreakSceneInstanceCommand>(SceneInstanceId{ id->Id.Value }, doc));
            },
            .Enabled = primaryIsInstance });
    // The origin a placement of this scene pivots around.
    offer("scene.origin", "Set Scene Origin to Primary", {},
          { .Invoke = [&w, document] {
                EditorDocument& doc = document();
                EditorScene& scene = doc.GetScene();
                if (auto command = MakeSetSceneOriginCommand(scene, doc, scene.ComposeWorldTransform(Primary(w)).Position))
                    w.Commands.Execute(std::move(command));
            },
            .Enabled = [&w] { return Primary(w).IsValid(); } });
}

void EntityVerbs::OfferZoneMoves(ActionCatalog& catalog, ActionContext& context)
{
    std::vector<std::string> moves;
    std::vector<std::string> names;
    if (Workspace.World.IsWorld())
        for (const ZoneHeader& zone : Workspace.World.Manifest().Zones)
        {
            moves.push_back(std::format("zone.move.{}", zone.Id.Value));
            names.push_back(zone.Name);
        }
    if (moves == OfferedMoves && names == OfferedNames)
        return;

    for (const std::string& move : OfferedMoves)
        if (std::ranges::find(moves, move) == moves.end())
            context.Release(move);
    for (std::size_t i = 0; i < moves.size(); ++i)
    {
        const ZoneId zone = Workspace.World.Manifest().Zones[i].Id;
        const std::string label = "Move to " + names[i];
        if (const ActionDefinition* existing = catalog.Find(moves[i]); existing != nullptr && existing->Label != label)
            (void)catalog.Relabel(moves[i], label);
        OfferAction(catalog, context, { .Id = moves[i], .Label = label, .Category = "Zone" },
                    { .Invoke = [this, zone] {
                          const std::vector<EntityId> entities = SelectedEntities(Workspace);
                          if (auto command = MakeMoveEntitiesToZoneCommand(entities, Workspace.World, zone,
                                                                           Workspace.Selection))
                              Workspace.Commands.Execute(std::move(command));
                      },
                      // A move is to a loaded zone the selection is not already in.
                      .Enabled = [this, zone] {
                          return !SelectedEntities(Workspace).empty() && zone != Workspace.World.FocusZone()
                              && Workspace.World.IsZoneOpen(zone);
                      } });
    }
    OfferedMoves = std::move(moves);
    OfferedNames = std::move(names);
}

std::vector<std::string> EntityVerbs::Relevant() const
{
    std::vector<std::string> verbs{ "entity.parent", "entity.unparent", "entity.visible", "entity.lock" };
    const EntityId primary = Primary(Workspace);
    if (primary.IsValid() && Workspace.World.FocusDocument().IsSceneInstanceRoot(primary))
    {
        verbs.emplace_back("scene.open_source");
        verbs.emplace_back("scene.break");
    }
    verbs.insert(verbs.end(), OfferedMoves.begin(), OfferedMoves.end());
    return verbs;
}
