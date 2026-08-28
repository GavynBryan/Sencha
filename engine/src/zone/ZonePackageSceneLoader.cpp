#include <zone/ZonePackageSceneLoader.h>

#include <core/identity/Id.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneFormat.h>
#include <world/build/EntityBuildPackage.h>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace
{
void SetError(SceneLoadError* error, std::string message)
{
    if (error != nullptr)
        error->Message = std::move(message);
}
} // namespace

bool BuildEntityPackageFromSceneJson(
    const JsonValue& root,
    const ComponentSerializerRegistry& serializers,
    EntityBuildPackage& package,
    SceneLoadError* error)
{
    if (!root.IsObject())
    {
        SetError(error, "Scene JSON root must be an object.");
        return false;
    }

    const JsonValue* version = root.Find("version");
    const JsonValue* entitiesValue = root.Find("entities");
    if (version == nullptr || !version->IsNumber()
        || static_cast<std::uint32_t>(version->AsNumber()) != SceneVersion
        || entitiesValue == nullptr || !entitiesValue->IsArray())
    {
        SetError(error, "Scene JSON has an invalid version or entity list.");
        return false;
    }

    EntityBuildPackage built;
    std::vector<PackageEntityId> entities;
    entities.reserve(entitiesValue->AsArray().size());

    for (const JsonValue& entityValue : entitiesValue->AsArray())
    {
        if (!entityValue.IsObject())
        {
            SetError(error, "Scene JSON entity must be an object.");
            return false;
        }

        const PackageEntityId entity = built.CreateEntity();
        entities.push_back(entity);

        const JsonValue* components = entityValue.Find("components");
        if (components == nullptr)
            continue;
        if (!components->IsObject())
        {
            SetError(error, "Scene JSON components must be an object.");
            return false;
        }

        for (const auto& [key, componentData] : components->AsObject())
        {
            IComponentSerializer* serializer = serializers.FindByJsonKey(key);
            if (serializer == nullptr)
            {
                SetError(
                    error,
                    "Scene JSON references an unregistered component '"
                        + key + "'.");
                return false;
            }

            if (!built.AddSerializedJson(
                    entity,
                    serializer->TypeId(),
                    componentData))
            {
                SetError(
                    error,
                    "Scene JSON contains a duplicate component on one entity.");
                return false;
            }

            // Lift the identity into package metadata (the component itself
            // still imports normally). A malformed id leaves the metadata
            // invalid here rather than failing the build; the same string then
            // fails the component's strict codec at import, which rejects the
            // zone before metadata and component could disagree.
            if (key == "persistent_id" && componentData.IsObject())
            {
                if (const JsonValue* id = componentData.Find("id");
                    id != nullptr && id->IsString())
                {
                    if (const auto parsed = PersistentEntityIdFromString(id->AsString()))
                        (void)built.SetPersistentId(entity, *parsed);
                }
            }
        }
    }

    const JsonValue* hierarchyValue = root.Find("hierarchy");
    if (hierarchyValue != nullptr && !hierarchyValue->IsArray())
    {
        SetError(error, "Scene JSON hierarchy must be an array.");
        return false;
    }

    if (hierarchyValue != nullptr)
    {
        for (const JsonValue& relation : hierarchyValue->AsArray())
        {
            if (!relation.IsObject())
            {
                SetError(error, "Scene JSON hierarchy relation must be an object.");
                return false;
            }

            const JsonValue* child = relation.Find("child");
            const JsonValue* parent = relation.Find("parent");
            if (child == nullptr || parent == nullptr
                || !child->IsNumber() || !parent->IsNumber())
            {
                SetError(error, "Scene JSON hierarchy relation is invalid.");
                return false;
            }

            const auto childIndex = static_cast<std::size_t>(child->AsNumber());
            const auto parentIndex = static_cast<std::size_t>(parent->AsNumber());
            if (childIndex >= entities.size() || parentIndex >= entities.size())
            {
                SetError(error, "Scene JSON hierarchy references an unknown entity.");
                return false;
            }

            if (!built.SetParent(entities[childIndex], entities[parentIndex]))
            {
                SetError(error, "Scene JSON hierarchy contains an invalid parent relation.");
                return false;
            }
        }
    }

    package = std::move(built);
    if (error != nullptr)
        error->Message.clear();
    return true;
}
