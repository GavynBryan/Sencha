#pragma once

#include "commands/ICommand.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/EntityNameComponent.h"

#include <ecs/EntityId.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>

// Sets or clears an entity's authored name as one undoable step. The component
// exists only while the entity has a non-empty name, so clearing removes it
// rather than storing an empty string every unnamed entity would then carry.
class RenameEntityCommand : public ICommand
{
public:
    RenameEntityCommand(EntityId entity, std::optional<std::string> before,
                        std::optional<std::string> after,
                        EditorScene& scene, EditorDocument& document)
        : Entity(entity)
        , Before(std::move(before))
        , After(std::move(after))
        , Scene(scene)
        , Document(document)
    {
    }

    void Execute() override { Apply(After); }
    void Undo() override { Apply(Before); }

private:
    void Apply(const std::optional<std::string>& name)
    {
        World& world = Scene.GetRegistry().Components;
        if (name.has_value())
        {
            if (EntityNameComponent* existing = world.TryGet<EntityNameComponent>(Entity))
                existing->Value = *name;
            else
                world.AddComponent(Entity, EntityNameComponent{ InlineString<64>(*name) });
        }
        else if (world.TryGet<EntityNameComponent>(Entity) != nullptr)
        {
            world.RemoveComponent<EntityNameComponent>(Entity);
        }
        Document.MarkDirty();
    }

    EntityId Entity;
    std::optional<std::string> Before;
    std::optional<std::string> After;
    EditorScene& Scene;
    EditorDocument& Document;
};

// Builds the rename, or nullptr when it would change nothing. A whitespace-only
// or empty new name clears the component.
[[nodiscard]] inline std::unique_ptr<ICommand> MakeRenameEntityCommand(
    EntityId entity, std::string_view newName, EditorScene& scene, EditorDocument& document)
{
    if (!scene.HasEntity(entity))
        return nullptr;

    std::optional<std::string> before;
    if (const auto* existing =
            scene.GetRegistry().Components.TryGet<EntityNameComponent>(entity))
        before = std::string(existing->Value.View());

    std::optional<std::string> after;
    const auto trimmedBegin = newName.find_first_not_of(" \t");
    if (trimmedBegin != std::string_view::npos)
        after = std::string(newName.substr(trimmedBegin,
            newName.find_last_not_of(" \t") - trimmedBegin + 1));

    if (before == after)
        return nullptr;
    return std::make_unique<RenameEntityCommand>(entity, std::move(before),
                                                 std::move(after), scene, document);
}
