#pragma once

#include "scene_source/SceneComposition.h"

class World;

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
