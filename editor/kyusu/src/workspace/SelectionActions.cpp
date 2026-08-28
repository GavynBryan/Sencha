#include "SelectionActions.h"

#include "commands/CommandStack.h"
#include "commands/CompositeCommand.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/WorldDocument.h"
#include "document/commands/BakeBrushToMeshCommand.h"
#include "document/commands/BreakInstanceCommand.h"
#include "document/commands/DuplicateEntitiesCommand.h"
#include "document/commands/MergeBrushesCommand.h"
#include "document/commands/SeparateFacesCommand.h"
#include "meshedit/IMeshEditTarget.h"
#include "meshedit/MeshEditService.h"
#include "selection/SelectionService.h"

#include <assets/runtime/RuntimeAssets.h>

#include <filesystem>
#include <utility>

SelectionActions::SelectionActions(WorldDocument& world, SelectionService& selection,
                                   CommandStack& commands, MeshEditService& meshEdit,
                                   PendingBridgeEdit& bridgeEdit,
                                   std::function<IMeshEditTarget*()> editTarget,
                                   std::function<void(std::vector<EntitySnapshot>&)> duplicateRemap)
    : World(world)
    , Selection(selection)
    , Commands(commands)
    , MeshEdit(meshEdit)
    , BridgeEdit(bridgeEdit)
    , EditTargetResolver(std::move(editTarget))
    , DuplicateRemap(std::move(duplicateRemap))
{
}

void SelectionActions::SetAssetEnvironment(RuntimeAssets& assets, std::string contentRoot,
                                           LoggingProvider& logging)
{
    Assets = &assets;
    ContentRoot = std::move(contentRoot);
    Logging = &logging;
}

std::vector<EntityId> SelectionActions::SelectedBrushEntities() const
{
    const EditorScene& scene = World.FocusDocument().GetScene();
    std::vector<EntityId> entities;
    for (const SelectableRef& ref : Selection.GetSelection())
        if (ref.IsEntity() && scene.HasEntity(ref.Entity))
            entities.push_back(ref.Entity);
    return entities;
}

void SelectionActions::ExecuteAsOneStep(std::vector<std::unique_ptr<ICommand>> commands)
{
    if (commands.empty())
        return;
    if (commands.size() == 1)
        Commands.Execute(std::move(commands.front()));
    else
        Commands.Execute(std::make_unique<CompositeCommand>(std::move(commands)));
}

void SelectionActions::Duplicate(bool asInstance)
{
    EditorDocument& document = World.FocusDocument();
    const EditorScene& scene = document.GetScene();
    std::vector<EntityId> sources;
    std::vector<Transform3f> transforms;
    for (const EntityId entity : SelectedBrushEntities())
    {
        sources.push_back(entity);
        const Transform3f* transform = scene.TryGetWorldTransform(entity);
        transforms.push_back(transform != nullptr ? *transform : Transform3f::Identity());
    }
    if (sources.empty())
        return;

    Commands.Execute(std::make_unique<DuplicateEntitiesCommand>(
        sources, transforms, document.GetScene(), document, Selection, asInstance, DuplicateRemap));
}

void SelectionActions::DuplicateWithOffset(Vec3d offset)
{
    EditorDocument& document = World.FocusDocument();
    const EditorScene& scene = document.GetScene();
    std::vector<EntityId> sources;
    std::vector<Transform3f> transforms;
    for (const EntityId entity : SelectedBrushEntities())
    {
        sources.push_back(entity);
        const Transform3f* transform = scene.TryGetWorldTransform(entity);
        Transform3f placed = transform != nullptr ? *transform : Transform3f::Identity();
        placed.Position += offset;
        transforms.push_back(placed);
    }
    if (sources.empty())
        return;

    Commands.Execute(std::make_unique<DuplicateEntitiesCommand>(
        sources, transforms, document.GetScene(), document, Selection, false, DuplicateRemap));
}

void SelectionActions::RecordRepeatableDuplicate(Vec3d offset)
{
    if (offset.SqrMagnitude() <= 0.0f)
        return;
    LastRepeatable = [this, offset] { DuplicateWithOffset(offset); };
}

void SelectionActions::RepeatLast()
{
    if (LastRepeatable)
        LastRepeatable();
}

void SelectionActions::MakeUnique()
{
    EditorDocument& document = World.FocusDocument();
    EditorScene& scene = document.GetScene();
    std::vector<std::unique_ptr<ICommand>> commands;
    for (const EntityId entity : SelectedBrushEntities())
        if (auto command = MakeBreakInstanceCommand(scene, document, entity))
            commands.push_back(std::move(command));
    ExecuteAsOneStep(std::move(commands));
}

void SelectionActions::Merge()
{
    EditorDocument& document = World.FocusDocument();
    EditorScene& scene = document.GetScene();
    const SelectableRef primary = Selection.GetPrimarySelection();
    EntityId target = primary.IsEntity() && scene.TryGetBrushMesh(primary.Entity) != nullptr
        ? primary.Entity
        : EntityId{};
    std::vector<EntityId> sources;
    for (const SelectableRef& ref : Selection.GetSelection())
    {
        if (!ref.IsEntity() || scene.TryGetBrushMesh(ref.Entity) == nullptr)
            continue;
        if (!target.IsValid())
        {
            target = ref.Entity;
            continue;
        }
        if (ref.Entity != target)
            sources.push_back(ref.Entity);
    }

    if (auto command = MakeMergeBrushesCommand(target, sources, scene, document, Selection))
        Commands.Execute(std::move(command));
}

void SelectionActions::SeparateFaces()
{
    // Face refs of ONE brush (the mesh verbs' contract); the first entity with
    // face refs wins.
    EntityId source;
    std::vector<std::uint32_t> faces;
    for (const SelectableRef& ref : Selection.GetSelection())
    {
        if (!ref.IsFace())
            continue;
        if (!source.IsValid())
            source = ref.Entity;
        if (ref.Entity == source)
            faces.push_back(ref.ElementId);
    }

    EditorDocument& document = World.FocusDocument();
    if (auto command = MakeSeparateFacesCommand(source, faces, document.GetScene(), document, Selection))
    {
        Commands.Execute(std::move(command));
        // The face indices no longer resolve on the reshaped source.
        Selection.ClearMeshElementSelections();
    }
}

void SelectionActions::Bridge(int segments)
{
    IMeshEditTarget* target = EditTargetResolver ? EditTargetResolver() : nullptr;
    if (target == nullptr)
        return;

    EditorDocument& document = World.FocusDocument();
    const SelectionSnapshot selection = Selection.GetSnapshot();
    if (PendingBridgeEdit::SpansTwoBrushes(selection.Items))
    {
        // Across two brushes the result is a new brush rather than an edit of
        // either, so it stages as a preview the segment count can be tuned on.
        BridgeEdit.Begin(document.GetScene(), document, Commands, selection.Items, segments);
        return;
    }

    if (auto command = MeshEdit.ApplyVerb(*target, selection, MeshEditVerb::BridgeEdges,
                                          { .BridgeSegments = segments }))
    {
        Commands.Execute(std::move(command));
        Selection.ClearMeshElementSelections();
    }
}

void SelectionActions::Bake()
{
    if (Assets == nullptr || ContentRoot.empty() || Logging == nullptr)
        return;

    EditorDocument& document = World.FocusDocument();
    EditorScene& scene = document.GetScene();
    const std::filesystem::path contentRoot(ContentRoot);

    std::vector<std::unique_ptr<ICommand>> commands;
    for (const EntityId entity : SelectedBrushEntities())
    {
        if (scene.TryGetBrush(entity) == nullptr)
            continue;
        if (auto command = MakeBakeBrushToMeshCommand(scene, document, Assets->Assets,
                                                      Assets->Registry, *Logging, entity, contentRoot))
            commands.push_back(std::move(command));
    }
    ExecuteAsOneStep(std::move(commands));
}

void SelectionActions::RevertBaked()
{
    if (Assets == nullptr)
        return;

    EditorDocument& document = World.FocusDocument();
    EditorScene& scene = document.GetScene();
    std::vector<std::unique_ptr<ICommand>> commands;
    for (const EntityId entity : SelectedBrushEntities())
    {
        if (scene.TryGetBakedBrush(entity) == nullptr)
            continue;
        if (auto command = MakeRevertBakedBrushCommand(scene, document, Assets->Assets, entity))
            commands.push_back(std::move(command));
    }
    ExecuteAsOneStep(std::move(commands));
}

bool SelectionActions::HasBakedSelection() const
{
    const EditorScene& scene = World.FocusDocument().GetScene();
    for (const SelectableRef& ref : Selection.GetSelection())
        if (ref.IsEntity() && scene.TryGetBakedBrush(ref.Entity) != nullptr)
            return true;
    return false;
}

bool SelectionActions::HasInstancedSelection() const
{
    const EditorScene& scene = World.FocusDocument().GetScene();
    for (const SelectableRef& ref : Selection.GetSelection())
        if (ref.IsEntity() && scene.IsBrushInstanced(ref.Entity))
            return true;
    return false;
}

const BrushMesh* SelectionActions::SelectedExportMesh() const
{
    const EditorScene& scene = World.FocusDocument().GetScene();
    for (const SelectableRef& ref : Selection.GetSelection())
    {
        if (!ref.IsEntity())
            continue;
        if (const BrushMesh* mesh = scene.TryGetBrushMesh(ref.Entity))
            return mesh;
        if (const BrushMesh* dormant = scene.TryGetDormantBrushMesh(ref.Entity))
            return dormant;
    }
    return nullptr;
}
