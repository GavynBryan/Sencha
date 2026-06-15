#pragma once

#include "IEditorPanel.h"

#include <ecs/ComponentId.h>
#include <ecs/EntityId.h>

#include "../level/LevelScene.h"

#include <cstddef>
#include <vector>

class CommandStack;
class LevelDocument;
class SelectionService;
struct IComponentSerializer;

// Registry-driven inspector. For the selected entity it iterates the component
// serializer registry, and for each component present draws its type-erased
// RuntimeFields() over the component's raw bytes — so ANY component, engine or
// game-module, is shown and edited without the editor naming its type. Edits are
// undoable via RawComponentEditCommand. (docs/plans/sencha-level-editor/02 §5.3.)
class InspectorPanel : public IEditorPanel
{
public:
    InspectorPanel(LevelScene& scene,
                   LevelDocument& document,
                   SelectionService& selection,
                   CommandStack& commands);

    std::string_view GetTitle() const override;
    bool IsVisible() const override;
    void OnDraw() override;

private:
    void DrawComponent(const IComponentSerializer& serializer, EntityId entity);
    void DrawAddComponentMenu(EntityId entity);
    // Editor-owned mesh-edit verbs for the selected brush (extrude/clip/delete).
    // Interim index/axis-based UI until click-to-select face picking lands (2b).
    void DrawBrushTools(EntityId entity);
    void ResetEditState();

    LevelScene& Scene;
    LevelDocument& Document;
    SelectionService& Selection;
    CommandStack& Commands;

    // A single in-flight edit (only one widget drags at a time). Captured on
    // widget activation (pre-edit bytes), committed to a RawComponentEditCommand
    // when the drag finishes.
    ComponentId            EditingComponent = InvalidComponentId;
    std::vector<std::byte> EditBefore;
    bool                   EditActive = false;

    // Brush-tools UI state.
    int   BrushFaceIndex      = 0;
    float BrushExtrudeDistance = 1.0f;

    EntityId LastEntity = {};
    bool Visible = true;
};
