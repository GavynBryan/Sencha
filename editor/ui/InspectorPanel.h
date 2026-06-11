#pragma once

#include "IEditorPanel.h"

#include <ecs/EntityId.h>
#include <math/geometry/3d/Transform3d.h>
#include <render/Camera.h>
#include <world/transform/TransformComponents.h>

#include "../level/LevelScene.h"

#include <optional>

class CommandStack;
class LevelDocument;
class SelectionService;

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
    // In-progress edit state for one component type. Working holds the live
    // widget value while a drag is active; Original holds the pre-edit
    // snapshot used to build the undoable command on commit.
    template <typename T>
    struct ComponentEditState
    {
        std::optional<T> Working;
        std::optional<T> Original;

        void Reset()
        {
            Working.reset();
            Original.reset();
        }
    };

    template <typename T>
    void DrawComponentSection(const char* label, EntityId entity, const T* current,
                              ComponentEditState<T>& state);

    void ResetEditState();

    LevelScene& Scene;
    LevelDocument& Document;
    SelectionService& Selection;
    CommandStack& Commands;

    ComponentEditState<LocalTransform> TransformEdit;
    ComponentEditState<BrushComponent> BrushEdit;
    ComponentEditState<CameraComponent> CameraEdit;
    EntityId LastEntity = {};
    bool Visible = true;
};
