#include "DuplicateEntitiesCommand.h"

#include "document/EditorScene.h"
#include "selection/SelectableRef.h"
#include "selection/SelectionService.h"

#include <world/identity/PersistentIdComponent.h>
#include <world/registry/Registry.h>

#include <algorithm>
#include <cstdint>

DuplicateEntitiesCommand::DuplicateEntitiesCommand(std::span<const EntityId> sources,
                                                   std::span<const Transform3f> transforms,
                                                   EditorScene& scene, EditorDocument& document,
                                                   SelectionService& selection,
                                                   bool asInstance,
                                                   std::function<void(std::vector<EntitySnapshot>&)> remap)
    : Scene(scene)
    , Document(document)
    , Selection(selection)
    , Sources(sources.begin(), sources.end())
    , Transforms(transforms.begin(), transforms.end())
    , AsInstance(asInstance)
    , Remap(std::move(remap))
{
}

void DuplicateEntitiesCommand::Execute()
{
    // Capture once: snapshots are id-independent, so they stay valid across
    // undo/redo even though each Execute mints fresh copies under new ids.
    if (!Captured)
    {
        PreviousSelection = Selection.GetSnapshot();

        // A source means its branch. Expand each to its subtree, parents before
        // children, skipping members already covered by an earlier source so a
        // parent-and-child selection copies the branch once.
        std::vector<EntityId> expanded;
        for (std::size_t i = 0; i < Sources.size(); ++i)
        {
            std::vector<EntityId> subtree;
            Scene.CollectSubtree(Sources[i], subtree);
            for (EntityId member : subtree)
            {
                const auto seen = std::find(expanded.begin(), expanded.end(), member);
                if (seen != expanded.end())
                {
                    // Selected child of a selected ancestor: it rides along, but
                    // it still owns its transform pairing if it was passed.
                    if (member == Sources[i])
                        RootOf[static_cast<std::size_t>(seen - expanded.begin())] = i;
                    continue;
                }
                expanded.push_back(member);
                RootOf.push_back(member == Sources[i] ? i : SIZE_MAX);
            }
        }

        Snapshots.reserve(expanded.size());
        SourceIds.reserve(expanded.size());
        const World& world = Scene.GetRegistry().Components;
        for (EntityId source : expanded)
        {
            Snapshots.push_back(Document.CaptureEntity(source));
            const auto* id = world.TryGet<PersistentIdComponent>(source);
            SourceIds.push_back(id != nullptr ? id->Id : PersistentEntityId{});
        }
        if (Remap)
            Remap(Snapshots);
        Captured = true;
    }

    Created.clear();
    Created.reserve(Snapshots.size());
    std::vector<SelectableRef> selection;
    selection.reserve(Sources.size());
    const RegistryId registry = Scene.GetRegistry().Id;
    for (std::size_t i = 0; i < Snapshots.size(); ++i)
    {
        const EntityId copy = Document.RestoreEntity(Snapshots[i], /*freshMesh*/ !AsInstance);

        // RestoreEntity resolved the snapshot's parent against live identity,
        // which for a copied branch is the ORIGINAL parent. Rebind interior
        // links to the copy of that parent; a root whose parent is outside the
        // branch keeps it (duplicating a child leaves the copy beside it).
        for (std::size_t j = 0; j < i; ++j)
            if (SourceIds[j].IsValid() && Snapshots[i].ParentId == SourceIds[j])
            {
                (void)Scene.SetParent(copy, Created[j]);
                break;
            }

        // Placement targets are world-space (they come from viewport gestures)
        // and apply to the passed roots; children keep their authored locals
        // and follow.
        if (RootOf[i] != SIZE_MAX && RootOf[i] < Transforms.size())
            Scene.SetWorldTransform(copy, Transforms[RootOf[i]]);

        Created.push_back(copy);
        if (RootOf[i] != SIZE_MAX)
            selection.push_back(SelectableRef::EntitySelection(registry, copy));
    }

    Selection.SetSelection(std::move(selection));
    Document.MarkDirty();
}

void DuplicateEntitiesCommand::Undo()
{
    // Leaf-up: destroying a parent copy first would trigger the scene's
    // orphan adoption on children that are about to die anyway.
    for (auto it = Created.rbegin(); it != Created.rend(); ++it)
        Scene.DestroyEntity(*it);
    Created.clear();
    Selection.ApplySnapshot(PreviousSelection);
    Document.MarkDirty();
}
