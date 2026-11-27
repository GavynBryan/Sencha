#include <world/serialization/EntityPatch.h>

#include <core/composition/CompositionPatch.h>
#include <core/composition/JsonComposition.h>
#include <core/json/JsonEquals.h>
#include <core/serialization/JsonArchive.h>
#include <ecs/World.h>
#include <world/build/EntityBuildPackage.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <algorithm>
#include <optional>
#include <utility>

namespace
{
    class PurposeInScope
    {
    public:
        PurposeInScope(SceneSerializationContext& context, SceneRecordPurpose purpose)
            : Context(context)
            , Previous(context.Purpose)
        {
            Context.Purpose = purpose;
        }
        ~PurposeInScope() { Context.Purpose = Previous; }
        PurposeInScope(const PurposeInScope&) = delete;
        PurposeInScope& operator=(const PurposeInScope&) = delete;

    private:
        SceneSerializationContext& Context;
        SceneRecordPurpose Previous;
    };

    [[nodiscard]] bool InScope(const IComponentSerializer& serializer, const EntityScope& scope)
    {
        return scope.Declared.Contains(serializer.Persistence())
            || std::ranges::find(scope.Listed, serializer.TypeId()) != scope.Listed.end();
    }

    [[nodiscard]] bool Carries(const World& world, EntityId entity, ComponentTypeId type)
    {
        const ComponentId column = world.GetComponentIdByType(type);
        return column != InvalidComponentId && world.HasComponent(entity, column);
    }

    [[nodiscard]] std::optional<JsonValue> Written(const IComponentSerializer& serializer, const World& world,
                                                   EntityId entity, SceneSerializationContext& context,
                                                   std::vector<std::string>* failed)
    {
        JsonWriteArchive archive;
        if (!serializer.Save(archive, entity, world, context) || !archive.Ok())
        {
            if (failed != nullptr)
                failed->emplace_back(serializer.JsonKey());
            return std::nullopt;
        }
        return archive.TakeValue();
    }

    void SortByKey(JsonValue& record)
    {
        std::ranges::sort(record.AsObject(), {}, &JsonValue::Object::value_type::first);
    }

    [[nodiscard]] bool SetAsideByRetune(std::string_view code)
    {
        return code == "remove_unknown_key" || code == "redeclares_inherited" || code == "removes_inherited"
            || code == "cannot_clear_required";
    }

    [[nodiscard]] std::optional<JsonValue> ComposedOver(const JsonValue& base, const JsonValue& patch,
                                                        const IComponentSerializer& serializer,
                                                        EntityPatchOutcome& outcome)
    {
        const CompositionLayerInput<JsonValue> under{ .Value = &base, .Asset = {}, .Provenance = nullptr };
        const CompositionLayerInput<JsonValue> over{ .Value = &patch, .Asset = {}, .Provenance = nullptr };
        CompositionReport report;
        JsonValue composed = ComposeLayers<JsonValue>(&under, {}, over, { .Schema = serializer.CompositionSchema() },
                                                      report);
        bool fits = true;
        for (const CompositionDiagnostic& diagnostic : report.Diagnostics)
        {
            std::string line = std::string(serializer.JsonKey()) + diagnostic.Path + ": " + diagnostic.Message + " ("
                + diagnostic.Code + ")";
            if (SetAsideByRetune(diagnostic.Code))
                outcome.Retuned.push_back(std::move(line));
            else
            {
                outcome.Misfit.push_back(std::move(line));
                fits = false;
            }
        }
        return fits ? std::optional<JsonValue>(std::move(composed)) : std::nullopt;
    }
}

std::optional<JsonValue> SavedDefaultRecord(const IComponentSerializer& serializer)
{
    if (serializer.Persistence() != ComponentPersistence::Saved)
        return std::nullopt;
    JsonWriteArchive archive;
    if (!serializer.SaveDefault(archive) || !archive.Ok())
        return std::nullopt;
    return archive.TakeValue();
}

std::vector<AuthoredComponent> AuthoredComponentsOf(const PackageEntity& entity,
                                                    const ComponentSerializerRegistry& serializers,
                                                    const EntityScope& scope)
{
    std::vector<AuthoredComponent> baseline;
    for (const PackageComponent& content : entity.Components)
        if (const IComponentSerializer* serializer = serializers.FindByType(content.Type);
            serializer != nullptr && InScope(*serializer, scope))
            baseline.push_back(
                { .Type = content.Type, .Record = content.SerializedJson.value_or(JsonValue()), .InContent = true, .Diffed = true });
    for (const ComponentTypeId listed : scope.Listed)
        if (entity.Find(listed) == nullptr && serializers.FindByType(listed) != nullptr)
            baseline.push_back({ .Type = listed, .Record = JsonValue(), .InContent = false, .Diffed = true });
    return baseline;
}

JsonValue WriteEntityRecord(const World& world, EntityId entity, const ComponentSerializerRegistry& serializers,
                            SceneSerializationContext& context, const EntityScope& scope, SceneRecordPurpose purpose,
                            std::vector<std::string>* failed)
{
    const PurposeInScope writing(context, purpose);
    JsonValue record(JsonValue::Object{});
    for (const auto& serializer : serializers.Entries())
    {
        if (!InScope(*serializer, scope) || !Carries(world, entity, serializer->TypeId()))
            continue;
        if (std::optional<JsonValue> value = Written(*serializer, world, entity, context, failed);
            value.has_value() && !value->IsNull())
            record.AsObject().emplace_back(std::string(serializer->JsonKey()), std::move(*value));
    }
    SortByKey(record);
    return record;
}

bool ReadEntityRecord(World& world, EntityId entity, const JsonValue& record,
                      const ComponentSerializerRegistry& serializers, SceneSerializationContext& context,
                      std::string* failed)
{
    if (!record.IsObject())
        return false;
    for (const auto& [key, value] : record.AsObject())
    {
        IComponentSerializer* serializer = serializers.FindByJsonKey(key);
        JsonReadArchive archive(value);
        if (serializer == nullptr || !serializer->LoadIntoWorld(archive, entity, world, context) || !archive.Ok())
        {
            if (failed != nullptr)
                *failed = key;
            return false;
        }
    }
    return true;
}

JsonValue CaptureEntityPatch(const World& world, EntityId entity, std::span<const AuthoredComponent> baseline,
                             ComponentPersistenceSet alsoCarried, const ComponentSerializerRegistry& serializers,
                             SceneSerializationContext& context, const JsonValue* kept,
                             std::vector<std::string>* failed, std::span<const ComponentTypeId> unchanged)
{
    const PurposeInScope writing(context, SceneRecordPurpose::Save);
    JsonValue authored(JsonValue::Object{});
    JsonValue live(JsonValue::Object{});
    JsonValue::Object keptMembers;
    const auto take = [&](const IComponentSerializer& serializer, const AuthoredComponent* content, bool carried) {
        if (carried && std::ranges::find(unchanged, serializer.TypeId()) != unchanged.end())
        {
            if (const JsonValue* member = kept != nullptr && kept->IsObject() ? kept->Find(serializer.JsonKey()) : nullptr)
                keptMembers.emplace_back(std::string(serializer.JsonKey()), *member);
            return;
        }
        std::optional<JsonValue> now;
        if (carried && !(now = Written(serializer, world, entity, context, failed)).has_value())
            return;
        std::optional<JsonValue> fallback;
        const JsonValue* was = nullptr;
        if (content != nullptr && content->InContent)
            was = &content->Record;
        else if (carried && (fallback = SavedDefaultRecord(serializer)).has_value())
            was = &*fallback;
        if (was == nullptr && !now.has_value())
            return;
        if (was != nullptr && now.has_value() && !now->IsNull() && JsonEquals(*was, *now))
            return;
        const std::string key(serializer.JsonKey());
        if (was != nullptr)
            authored.AsObject().emplace_back(key, *was);
        if (now.has_value() && !now->IsNull())
            live.AsObject().emplace_back(key, std::move(*now));
    };
    for (const AuthoredComponent& component : baseline)
        if (const IComponentSerializer* serializer = serializers.FindByType(component.Type);
            serializer != nullptr && component.Diffed)
            take(*serializer, &component, Carries(world, entity, component.Type));
    for (const auto& serializer : serializers.Entries())
    {
        if (!alsoCarried.Contains(serializer->Persistence()) || !Carries(world, entity, serializer->TypeId()))
            continue;
        const bool listed = std::ranges::any_of(
            baseline, [&](const AuthoredComponent& component) { return component.Type == serializer->TypeId(); });
        if (!listed)
            take(*serializer, nullptr, true);
    }
    JsonValue patch = PatchFrom(authored, live, &serializers.EntitySchema()).value_or(JsonValue(JsonValue::Object{}));
    for (auto& member : keptMembers)
        patch.AsObject().push_back(std::move(member));
    if (kept != nullptr && kept->IsObject())
        for (const auto& [key, member] : kept->AsObject())
            if (serializers.FindByJsonKey(key) == nullptr)
                patch.AsObject().emplace_back(key, member);
    SortByKey(patch);
    return patch;
}

EntityPatchOutcome ApplyEntityPatch(PackageEntity& entity, const JsonValue& patch,
                                    const ComponentSerializerRegistry& serializers)
{
    EntityPatchOutcome outcome;
    if (!patch.IsObject())
        return outcome;
    for (const auto& [key, member] : patch.AsObject())
    {
        const IComponentSerializer* serializer = serializers.FindByJsonKey(key);
        if (serializer == nullptr)
        {
            outcome.Unknown.push_back(key);
            continue;
        }
        const ComponentTypeId type = serializer->TypeId();
        const PackageComponent* content = entity.Find(type);
        if (member.IsNull())
        {
            (void)entity.Remove(type);
            continue;
        }
        std::optional<JsonValue> base;
        if (content != nullptr && content->SerializedJson.has_value())
            base = content->SerializedJson;
        else if (content == nullptr)
            base = SavedDefaultRecord(*serializer);
        std::optional<JsonValue> value = base.has_value() ? ComposedOver(*base, member, *serializer, outcome) : member;
        if (!value.has_value())
        {
            outcome.MisfitMembers.push_back(key);
            continue;
        }
        const bool unpatchedAlready = std::ranges::any_of(
            entity.Unpatched, [type](const PackageComponent& component) { return component.Type == type; });
        if (content != nullptr && !unpatchedAlready)
            entity.Unpatched.push_back(*content);
        entity.SetSerialized(type, std::move(*value));
    }
    return outcome;
}
