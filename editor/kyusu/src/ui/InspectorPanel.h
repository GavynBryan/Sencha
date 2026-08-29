#pragma once

#include "ui/IEditorPanel.h"

#include <ecs/ComponentId.h>
#include <ecs/EntityId.h>

#include "document/EditorScene.h"

#include <cstddef>
#include <unordered_map>
#include <vector>

class CommandStack;
class WorldDocument;
class SelectionService;
class EditorComponentAdapterRegistry;
struct IComponentSerializer;
struct RuntimeField;

// Registry-driven inspector. For the selected entity it iterates the component
// serializer registry, and for each component present draws its type-erased
// RuntimeFields() over the component's raw bytes — so ANY component, engine or
// game-module, is shown and edited without the editor naming its type. Edits are
// undoable via RawComponentEditCommand. (docs/plans/sencha-level-editor/02 §5.3.)
class InspectorPanel : public IEditorPanel
{
public:
    InspectorPanel(WorldDocument& world,
                   SelectionService& selection,
                   CommandStack& commands,
                   EditorComponentAdapterRegistry& adapters);

    std::string_view GetTitle() const override;
    void OnDraw() override;
    DockSlot GetDockSlot() const override { return DockSlot::RightBottom; }
    // Shares the lower-right node with the lighting panel (tabbed).
    int GetDockTabGroup() const override { return 0; }

private:
    void DrawComponent(IComponentSerializer& serializer, EntityId entity);

    // The source's bytes for one of the inspected member's components --
    // what "no override" looks like, materialized once and cached. The
    // generational entity key self-invalidates across projection rebuilds
    // (a rebuilt member has a new handle); the cache clears whenever the
    // inspected entity changes.
    struct BaselineEntry
    {
        bool Present = false; // the source defines this component at all
        std::vector<std::byte> Bytes;
    };
    const BaselineEntry& BaselineFor(IComponentSerializer& serializer,
                                     EntityId entity, ComponentId component);
    std::unordered_map<ComponentId, BaselineEntry> BaselineCache;
    EntityId BaselineEntity = {};
    // Picker for an asset-handle field (RuntimeField tagged with an AssetType):
    // a combo of scanned assets of that type, applied via AssetFieldEditCommand.
    void DrawAssetField(const RuntimeField& field, EntityId entity,
                        ComponentId component, float labelWidth);
    void DrawAddComponentMenu(EntityId entity);
    void ResetEditState();

    WorldDocument& WorldDoc;
    SelectionService& Selection;
    CommandStack& Commands;
    EditorComponentAdapterRegistry& Adapters;

    // A single in-flight edit (only one widget drags at a time). Captured on
    // widget activation (pre-edit bytes), committed to a RawComponentEditCommand
    // when the drag finishes. EditingEntity is the entity those bytes belong to,
    // so an interrupted edit can be reverted to them (see ResetEditState).
    EntityId               EditingEntity = {};
    ComponentId            EditingComponent = InvalidComponentId;
    std::vector<std::byte> EditBefore;
    bool                   EditActive = false;

    // A component remove requested this frame, executed after the component loop
    // (removal is a structural change, so it must not run mid-iteration). Points
    // at a process-global serializer entry, so it never dangles.
    IComponentSerializer*  PendingRemoval = nullptr;

    EntityId LastEntity = {};
};
