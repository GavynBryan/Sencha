#include "LevelSubjects.h"

#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/EntityNameComponent.h"
#include "document/EntityChangeSet.h"
#include "document/SpaceComponents.h"
#include "document/commands/RenameEntityCommand.h"

#include <core/identity/Id.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/registry/Registry.h>

#include <algorithm>
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

SubjectResolution ResolveEntitySubjects(const EditorDocument& document, const JsonValue& subjects)
{
    SubjectResolution all;
    if (!subjects.IsArray())
        return all;
    for (const JsonValue& subject : subjects.AsArray())
    {
        SubjectResolution one = ResolveEntitySubject(document, subject.IsString() ? subject.AsString() : std::string{});
        if (!one.Resolved())
            return one;
        for (const EntityId entity : one.Entities)
            if (std::ranges::find(all.Entities, entity) == all.Entities.end())
                all.Entities.push_back(entity);
    }
    return all;
}

EditorOperationOutcome SubjectRefusal(const SubjectResolution& subject)
{
    JsonValue detail;
    if (!subject.Candidates.empty())
    {
        JsonValue::Array candidates;
        for (const std::string& id : subject.Candidates)
            candidates.push_back(JsonValue(id));
        detail = JsonValue(JsonValue::Object{ { "candidates", JsonValue(std::move(candidates)) } });
    }
    return EditorOperationOutcome::Refused(subject.Code, subject.Message, std::move(detail));
}

std::variant<std::string, EditorOperationOutcome> CheckEntityName(const EditorDocument& document, EntityId entity,
                                                                 bool asSpace, std::string_view text)
{
    constexpr std::size_t kLongest = sizeof(EntityNameComponent::Value) - 1;
    std::string name = NormalizedEntityName(text);
    if (name.empty())
    {
        if (asSpace)
            return EditorOperationOutcome::Refused("space.name_invalid", "a space needs a name");
        return name;
    }
    if (name.size() > kLongest)
        return EditorOperationOutcome::Refused("entity.name_invalid",
                                               "a name is at most " + std::to_string(kLongest) + " characters");
    const EditorScene& scene = document.GetScene();
    const World& components = scene.GetRegistry().Components;
    for (const EntityId other : scene.GetAllEntities())
        if (other != entity && EntityNameText(document, other) == name
            && (asSpace || components.HasComponent<Space>(other)))
            return EditorOperationOutcome::Refused(
                "space.name_taken", "'" + name + "' is already the name of "
                    + (components.HasComponent<Space>(other) ? "a space" : "another entity")
                    + ", and a space's name must name only it");
    return name;
}

EditorOperationOutcome RenameEntity(EditorDocument& document, EntityChangeRecorder& recorder, EntityId entity,
                                    std::string_view text)
{
    const bool space = document.GetScene().GetRegistry().Components.HasComponent<Space>(entity);
    std::variant<std::string, EditorOperationOutcome> checked = CheckEntityName(document, entity, space, text);
    if (auto* refused = std::get_if<EditorOperationOutcome>(&checked))
        return std::move(*refused);
    recorder.Touch(entity);
    if (auto rename = MakeRenameEntityCommand(entity, std::get<std::string>(checked), document.GetScene(), document))
        rename->Execute();
    return EditorOperationOutcome::Ok({});
}
