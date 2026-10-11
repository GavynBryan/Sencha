#include "LevelSubjects.h"

#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/EntityNameComponent.h"

#include <core/identity/Id.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/registry/Registry.h>

#include <format>
#include <optional>

namespace
{
    SubjectResolution Unresolved(std::string code, std::string message)
    {
        SubjectResolution resolution;
        resolution.Code = std::move(code);
        resolution.Message = std::move(message);
        return resolution;
    }
}

std::string PersistentIdText(const EditorDocument& document, EntityId entity)
{
    const auto* id = document.GetRegistry().Components.TryGet<PersistentIdComponent>(entity);
    return id != nullptr ? PersistentEntityIdToString(id->Id) : std::string{};
}

std::string EntityNameText(const EditorDocument& document, EntityId entity)
{
    const auto* name = document.GetRegistry().Components.TryGet<EntityNameComponent>(entity);
    return name != nullptr ? std::string(name->Value.View()) : std::string{};
}

SubjectResolution ResolveEntitySubject(const EditorDocument& document, std::string_view subject)
{
    const EditorScene& scene = document.GetScene();
    if (const std::optional<PersistentEntityId> wanted = PersistentEntityIdFromString(subject))
    {
        for (const EntityId entity : scene.GetAllEntities())
        {
            const auto* id = document.GetRegistry().Components.TryGet<PersistentIdComponent>(entity);
            if (id != nullptr && id->Id == *wanted)
            {
                SubjectResolution resolution;
                resolution.Entities.push_back(entity);
                return resolution;
            }
        }
        return Unresolved("subject.not_found", std::format("no entity has the id {}", subject));
    }

    if (subject.starts_with('@') && subject.size() > 1)
    {
        const std::string_view name = subject.substr(1);
        SubjectResolution resolution;
        for (const EntityId entity : scene.GetAllEntities())
        {
            if (EntityNameText(document, entity) != name)
                continue;
            resolution.Entities.push_back(entity);
            resolution.Candidates.push_back(PersistentIdText(document, entity));
        }
        if (resolution.Entities.empty())
            return Unresolved("subject.not_found", std::format("no entity is named '{}'", name));
        if (resolution.Entities.size() > 1)
        {
            resolution.Code = "subject.ambiguous";
            resolution.Message = std::format("{} entities are named '{}'; name one by its id",
                                             resolution.Entities.size(), name);
            return resolution;
        }
        resolution.Candidates.clear();
        return resolution;
    }

    return Unresolved("subject.malformed",
                      std::format("'{}' is neither a 16-hex-digit id nor '@<name>'", subject));
}
