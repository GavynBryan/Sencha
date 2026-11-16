#pragma once

#include "scene_source/SceneComposition.h"

class World;

// What composition needs to know about a component, answered from the editor's
// serializer registry and the components this World registered.
class EditorSceneComponentRules final : public ISceneComponentRules
{
public:
    explicit EditorSceneComponentRules(const World& world)
        : Components(world)
    {
    }

    [[nodiscard]] const DataFieldSchema* SchemaOf(std::string_view component) const override;
    [[nodiscard]] std::vector<std::string> OwedBy(std::string_view component) const override;
    [[nodiscard]] bool IsRemovable(std::string_view component) const override;

private:
    const World& Components;
};
