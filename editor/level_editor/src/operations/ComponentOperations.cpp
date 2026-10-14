#include "ComponentOperations.h"

#include "LevelSubjects.h"
#include "LevelTarget.h"

#include "document/EditorDocument.h"
#include "document/EntityChangeSet.h"

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/metadata/DataSchemaFields.h>

#include <string>
#include <string_view>

namespace
{
    JsonValue KeysOf(const JsonValue& object)
    {
        JsonValue::Array keys;
        if (object.IsObject())
            for (const auto& [key, value] : object.AsObject())
                keys.push_back(JsonValue(key));
        return JsonValue(JsonValue::Object{ { "fields", JsonValue(std::move(keys)) } });
    }

    std::string_view KindName(const JsonValue& value)
    {
        if (value.IsBool())
            return "true or false";
        if (value.IsNumber())
            return "a number";
        if (value.IsString())
            return "text";
        if (value.IsArray())
            return "a list";
        if (value.IsObject())
            return "an object";
        return "null";
    }

    bool SameKind(const JsonValue& a, const JsonValue& b)
    {
        if (a.IsArray() && b.IsArray())
            return a.AsArray().size() == b.AsArray().size();
        return KindName(a) == KindName(b);
    }

    // The member `path` names inside `component`, dot by dot; null with the
    // fields that were there to choose from when a step is missing.
    JsonValue* Walk(JsonValue& component, std::string_view path, JsonValue& offered)
    {
        JsonValue* at = &component;
        while (!path.empty())
        {
            const std::size_t dot = path.find('.');
            const std::string_view step = path.substr(0, dot);
            JsonValue* next = at->IsObject() ? at->Find(step) : nullptr;
            if (next == nullptr)
            {
                offered = KeysOf(*at);
                return nullptr;
            }
            at = next;
            path = dot == std::string_view::npos ? std::string_view{} : path.substr(dot + 1);
        }
        return at;
    }

    EditorOperationEntry Set(LevelTarget& level)
    {
        EditorOperationEntry entry;
        entry.Definition.Name = "component.set";
        entry.Definition.Kind = EditorOperationKind::Mutation;
        entry.Definition.Category = "Level";
        entry.Definition.Description =
            "Sets one field of one of an entity's components, as one undoable step: a light's intensity, a "
            "space's wall thickness. Components and fields are named as entity.describe shows them; a nested "
            "field is named with dots. The value must be the kind the field already holds.";
        entry.Definition.Parameters = DataField::Record({}, {}, {}, {
            DataField::Field("subject", DataFieldKind::String, {}, "an entity id or '@<name>'"),
            DataField::Field("component", DataFieldKind::String, {}, "the component's key, as entity.describe lists it"),
            DataField::Field("field", DataFieldKind::String, {}, "the field, e.g. 'thickness' or 'color.r'"),
            DataField::Field("value", DataFieldKind::String, {}, "the new value as JSON: 0.5, true, [1, 0, 0], \"text\""),
        });
        entry.Definition.Result = MutationResultSchema({
            DataField::Field("stored", DataFieldKind::String, {}, "the field as the component holds it now, as JSON") });
        entry.Execute = [&level, name = entry.Definition.Name](const JsonValue& arguments) {
            return level.Mutate(name, arguments, [&arguments](EditorDocument& document, EntityChangeRecorder& recorder) {
                const SubjectResolution subject = ResolveEntitySubject(document, arguments.Find("subject")->AsString());
                if (!subject.Resolved())
                    return SubjectRefusal(subject);
                const EntityId entity = subject.Entities.front();
                const std::string& key = arguments.Find("component")->AsString();
                const std::string& field = arguments.Find("field")->AsString();

                EntitySnapshot snapshot = document.CaptureEntity(entity);
                JsonValue* component = snapshot.Components.Find(key);
                if (component == nullptr)
                    return EditorOperationOutcome::Refused("component.absent", "the entity has no '" + key + "' component",
                                                           KeysOf(snapshot.Components));
                JsonValue offered;
                JsonValue* target = Walk(*component, field, offered);
                if (target == nullptr)
                    return EditorOperationOutcome::Refused("component.field_unknown",
                                                           "'" + key + "' has no field '" + field + "'", std::move(offered));
                const std::optional<JsonValue> value = JsonParse(arguments.Find("value")->AsString());
                if (!value)
                    return EditorOperationOutcome::Refused("component.value_invalid", "the value is not JSON");
                if (!SameKind(*target, *value))
                    return EditorOperationOutcome::Refused("component.value_invalid",
                                                           "'" + field + "' holds " + std::string(KindName(*target))
                                                               + (target->IsArray() ? " of " + std::to_string(target->AsArray().size()) : ""));
                *target = *value;
                recorder.Touch(entity);
                document.OverwriteEntity(entity, snapshot);
                document.MarkDirty();

                EntitySnapshot written = document.CaptureEntity(entity);
                JsonValue unused;
                const JsonValue* stored = written.Components.Find(key) != nullptr
                    ? Walk(*written.Components.Find(key), field, unused) : nullptr;
                return EditorOperationOutcome::Ok(JsonValue(JsonValue::Object{
                    { "stored", JsonValue(stored != nullptr ? JsonStringify(*stored) : std::string("null")) } }));
            });
        };
        return entry;
    }
}

std::vector<EditorOperationEntry> ComponentOperations(LevelTarget& level)
{
    std::vector<EditorOperationEntry> entries;
    entries.push_back(Set(level));
    return entries;
}
