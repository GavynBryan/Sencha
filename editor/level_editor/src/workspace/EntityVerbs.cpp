#include "workspace/EntityVerbs.h"

#include "actions/ActionCatalog.h"
#include "actions/ActionOffer.h"
#include "commands/CompositeCommand.h"
#include "document/commands/MoveEntitiesToZoneCommand.h"
#include "document/commands/PlaceEntityCommand.h"
#include "document/commands/ReparentEntitiesCommand.h"
#include "document/commands/SceneInstanceCommands.h"
#include "document/commands/SetSceneOriginCommand.h"
#include "document/commands/EntityFlagCommand.h"
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
    const ActionOffers offer{ catalog, context, "Entity" };
    const auto document = [&w]() -> EditorDocument& { return w.World.FocusDocument(); };
    const auto anySelected = [&w] { return !SelectedEntities(w).empty(); };
    const auto primaryIsInstance = [&w, document] {
        const EntityId primary = Primary(w);
        return primary.IsValid() && document().IsSceneInstanceRoot(primary);
    };

    offer({ .Id = "entity.create", .Label = "New Entity",
            .DefaultChords = { { SDLK_N, { .Ctrl = true, .Shift = true } } } },
          { .Invoke = [&w, document] {
                w.Commands.Execute(std::make_unique<PlaceEntityCommand>(std::string{}, Transform3f::Identity(),
                                                                        document(), w.Selection));
            } });

    // Everything selected but the primary goes under it: the primary is the
    // one picked last, as a parent is.
    offer({ .Id = "entity.parent", .Label = "Parent to Primary", .DefaultChords = { { SDLK_P, { .Ctrl = true } } } },
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
    offer({ .Id = "entity.unparent", .Label = "Clear Parent", .DefaultChords = { { SDLK_P, { .Alt = true } } } },
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
    const auto flip = [&w, document](EntityFlag flag) {
        EditorDocument& doc = document();
        const EntityId primary = Primary(w);
        if (!primary.IsValid())
            return;
        const std::vector<EntityId> selected = SelectedEntities(w);
        if (std::unique_ptr<ICommand> step =
                MakeSetEntityFlagCommand(selected, flag, !EntityFlagOf(doc.GetScene(), primary, flag), doc))
            w.Commands.Execute(std::move(step));
    };
    offer({ .Id = "entity.visible", .Label = "Hide or Show" },
          { .Invoke = [flip] { flip(EntityFlag::Visible); }, .Enabled = anySelected });
    offer({ .Id = "entity.lock", .Label = "Lock or Unlock" },
          { .Invoke = [flip] { flip(EntityFlag::Locked); }, .Enabled = anySelected });

    offer({ .Id = "scene.open_source", .Label = "Open Source" },
          { .Invoke = [this, &w, document] {
                const std::string source = document().SceneInstanceSourceOf(Primary(w));
                if (OpenSource && !OpenSource(source))
                    std::fprintf(stderr, "[editor] source '%s' not found under any content root\n", source.c_str());
            },
            .Enabled = primaryIsInstance });
    offer({ .Id = "scene.break", .Label = "Break Scene Instance" },
          { .Invoke = [&w, document] {
                EditorDocument& doc = document();
                const World& world = doc.GetScene().GetRegistry().Components;
                if (const auto* id = world.TryGet<PersistentIdComponent>(Primary(w)))
                    w.Commands.Execute(std::make_unique<BreakSceneInstanceCommand>(SceneInstanceId{ id->Id.Value }, doc));
            },
            .Enabled = primaryIsInstance });
    // The origin a placement of this scene pivots around.
    offer({ .Id = "scene.origin", .Label = "Set Scene Origin to Primary" },
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
    std::vector<ActionDefinition> moves;
    std::vector<ZoneId> zones;
    if (Workspace.World.IsWorld())
        for (const ZoneHeader& zone : Workspace.World.Manifest().Zones)
        {
            moves.push_back({ .Id = std::format("zone.move.{}", zone.Id.Value),
                              .Label = "Move to " + zone.Name,
                              .Category = "Zone" });
            zones.push_back(zone.Id);
        }
    ZoneMoves.Offer(catalog, context, std::move(moves), [this, &zones](std::size_t index) {
        const ZoneId zone = zones[index];
        return ActionBinding{
            .Invoke = [this, zone] {
                const std::vector<EntityId> entities = SelectedEntities(Workspace);
                if (auto command = MakeMoveEntitiesToZoneCommand(entities, Workspace.World, zone, Workspace.Selection))
                    Workspace.Commands.Execute(std::move(command));
            },
            // A move is to a loaded zone the selection is not already in.
            .Enabled = [this, zone] {
                return !SelectedEntities(Workspace).empty() && zone != Workspace.World.FocusZone()
                    && Workspace.World.IsZoneOpen(zone);
            } };
    });
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
    verbs.insert(verbs.end(), ZoneMoves.Offered().begin(), ZoneMoves.Offered().end());
    return verbs;
}
