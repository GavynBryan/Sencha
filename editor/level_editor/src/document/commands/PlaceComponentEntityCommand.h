#pragma once

#include "commands/ICommand.h"
#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"
#include "document/EntityNameComponent.h"
#include "document/EntitySnapshot.h"
#include "selection/SelectableRef.h"
#include "selection/SelectionService.h"

#include <math/geometry/3d/Transform3d.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <optional>
#include <string>
#include <utility>

// Places a new entity carrying one placeable component at its defaults -- a
// light, a player start -- named after it and selected, as one step. The first
// execute builds it; a redo restores what that built, its identity included.
class PlaceComponentEntityCommand final : public ICommand
{
public:
    PlaceComponentEntityCommand(std::string componentKey, Transform3f placement, EditorDocument& document,
                                SelectionService& selection)
        : ComponentKey(std::move(componentKey))
        , Placement(placement)
        , Document(document)
        , Selection(selection)
    {
    }

    void Execute() override
    {
        PreviousSelection = Selection.GetSnapshot();
        Entity = Snapshot.has_value() ? Document.RestoreEntity(*Snapshot) : Build();
        if (!Entity.IsValid())
            return;
        if (!Snapshot.has_value())
            Snapshot = Document.CaptureEntity(Entity);
        Document.MarkDirty();
        Selection.SetSelection({ SelectableRef::EntitySelection(Document.GetRegistry().Id, Entity) });
    }

    void Undo() override
    {
        if (Entity.IsValid())
            Document.GetScene().DestroyEntity(Entity);
        Entity = {};
        Selection.ApplySnapshot(PreviousSelection);
        Document.MarkDirty();
    }

    [[nodiscard]] EntityId Placed() const { return Entity; }

private:
    [[nodiscard]] EntityId Build()
    {
        IComponentSerializer* serializer = EditorSceneSerializers().FindByJsonKey(ComponentKey);
        if (serializer == nullptr || serializer->PlacementLabel().empty())
            return {};
        EditorScene& scene = Document.GetScene();
        World& world = scene.GetRegistry().Components;
        serializer->RegisterStorage(scene.GetRegistry());
        const ComponentId component = world.GetComponentIdByType(serializer->TypeId());
        const ComponentMeta* meta = component != InvalidComponentId ? world.GetMeta(component) : nullptr;
        if (meta == nullptr)
            return {};

        const std::string name = scene.NextEntityName(serializer->PlacementLabel());
        const EntityId entity = scene.CreateEntity(Placement.Position);
        scene.SetTransform(entity, Placement);
        scene.SetComponent(entity, EntityNameComponent{ InlineString<64>(name) });
        const std::vector<std::byte> defaults = serializer->DefaultBytes();
        world.AddComponentRaw(entity, component, defaults.size() == meta->Size ? defaults.data() : nullptr,
                              meta->Size, meta->Alignment);
        return entity;
    }

    std::string ComponentKey;
    Transform3f Placement;
    EditorDocument& Document;
    SelectionService& Selection;
    std::optional<EntitySnapshot> Snapshot;
    SelectionSnapshot PreviousSelection;
    EntityId Entity;
};
