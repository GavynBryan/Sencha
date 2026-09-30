#pragma once

#include "commands/ICommand.h"
#include "document/EditorDocument.h"
#include "selection/SelectableRef.h"
#include "selection/SelectionService.h"

#include <math/geometry/3d/Transform3d.h>
#include <world/identity/PersistentEntityIndex.h>

#include <memory>
#include <string>
#include <utility>

// Places a scene source as one undoable step and selects the placement's root.
// The first Execute mints the instance and entity ids; redo restores the
// captured record verbatim, so identity is stable across the undo stack.
class PlaceSceneInstanceCommand : public ICommand
{
public:
    PlaceSceneInstanceCommand(std::string source, Transform3f placement,
                              EditorDocument& document, SelectionService& selection)
        : Source(std::move(source))
        , Placement(placement)
        , Document(document)
        , Selection(selection)
    {
    }

    void Execute() override
    {
        if (!Captured)
        {
            PreviousSelection = Selection.GetSnapshot();
            const SceneInstanceId id =
                Document.PlaceSceneInstance(Source, Placement);
            if (!id.IsValid())
                return;
            const SceneInstanceRecord* record = Document.FindSceneInstance(id);
            if (record != nullptr)
                Record = *record;
            Captured = true;
        }
        else
        {
            (void)Document.RestoreSceneInstance(Record);
        }
        SelectRoot();
    }

    void Undo() override
    {
        if (Record.Id.IsValid())
            (void)Document.RemoveSceneInstance(Record.Id, &Record);
        Selection.ApplySnapshot(PreviousSelection);
    }

    [[nodiscard]] bool Placed() const { return Captured && Record.Id.IsValid(); }

private:
    void SelectRoot()
    {
        if (!Record.Id.IsValid())
            return;
        const auto* index = Document.GetRegistry()
                                .Components.TryGetResource<PersistentEntityIndex>();
        if (index == nullptr)
            return;
        const EntityId root =
            index->TryResolve(PersistentEntityId{ Record.Id.Value });
        if (root.IsValid())
            Selection.SetSelection({ SelectableRef::EntitySelection(
                Document.GetRegistry().Id, root) });
    }

    std::string Source;
    Transform3f Placement;
    EditorDocument& Document;
    SelectionService& Selection;
    SceneInstanceRecord Record;
    SelectionSnapshot PreviousSelection;
    bool Captured = false;
};

// Severs a placement into plain local entities, undoably: undo destroys the
// severed entities and rebuilds the placement from the record captured at the
// break, edits included.
class BreakSceneInstanceCommand : public ICommand
{
public:
    BreakSceneInstanceCommand(SceneInstanceId instance, EditorDocument& document)
        : Instance(instance)
        , Document(document)
    {
    }

    void Execute() override { (void)Document.BreakSceneInstance(Instance, &Record); }

    void Undo() override
    {
        if (!Record.Id.IsValid())
            return;
        // The severed entities are exactly the record's contribution; the
        // root's subtree is all of them, nested content included.
        const auto* index = Document.GetRegistry()
                                .Components.TryGetResource<PersistentEntityIndex>();
        if (index != nullptr)
        {
            const EntityId root =
                index->TryResolve(PersistentEntityId{ Record.Id.Value });
            if (root.IsValid())
                Document.GetScene().DestroySubtree(root);
        }
        (void)Document.RestoreSceneInstance(Record);
    }

private:
    SceneInstanceId Instance;
    EditorDocument& Document;
    SceneInstanceRecord Record;
};
