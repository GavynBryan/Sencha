#pragma once

#include "commands/CompositeCommand.h"
#include "document/EditorDocument.h"
#include "document/commands/ValueCommand.h"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

// Whether an entity shows in the editor's views, and whether a pick can take it.
enum class EntityFlag : std::uint8_t
{
    Visible,
    Locked,
};

[[nodiscard]] inline bool EntityFlagOf(const EditorScene& scene, EntityId entity, EntityFlag flag)
{
    return flag == EntityFlag::Visible ? scene.IsEntityVisible(entity) : scene.IsEntityLocked(entity);
}

// One step setting `flag` on each of `entities` to `to`; null when none of them changes.
[[nodiscard]] inline std::unique_ptr<ICommand> MakeSetEntityFlagCommand(std::span<const EntityId> entities,
                                                                        EntityFlag flag, bool to,
                                                                        EditorDocument& document)
{
    EditorScene& scene = document.GetScene();
    std::vector<std::unique_ptr<ICommand>> steps;
    for (const EntityId entity : entities)
    {
        const bool now = EntityFlagOf(scene, entity, flag);
        if (now == to)
            continue;
        steps.push_back(std::make_unique<ValueCommand<bool>>(
            now, to,
            [&scene, entity, flag](const bool& value) {
                if (flag == EntityFlag::Visible)
                    scene.SetEntityVisible(entity, value);
                else
                    scene.SetEntityLocked(entity, value);
            },
            document));
    }
    if (steps.empty())
        return nullptr;
    if (steps.size() == 1)
        return std::move(steps.front());
    return std::make_unique<CompositeCommand>(std::move(steps));
}
