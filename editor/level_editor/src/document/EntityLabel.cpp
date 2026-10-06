#include "document/EntityLabel.h"

#include "document/EditorDocument.h"
#include "document/EntityNameComponent.h"
#include "scene_source/SceneSourcePaths.h"

#include <ecs/World.h>

std::string EntityLabel(const EditorDocument& document, EntityId entity)
{
    const World& world = document.GetScene().GetRegistry().Components;
    if (const auto* named = world.TryGet<EntityNameComponent>(entity); named != nullptr && !named->Value.Empty())
        return std::string(named->Value.View());
    if (document.IsSceneInstanceRoot(entity))
        if (const std::string source = document.SceneInstanceSourceOf(entity); !SceneSourceStem(source).empty())
            return std::string(SceneSourceStem(source));
    return "Entity " + std::to_string(entity.Index);
}
