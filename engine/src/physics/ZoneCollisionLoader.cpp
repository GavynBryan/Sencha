#include <physics/ZoneCollisionLoader.h>

#include <cstddef>
#include <fstream>
#include <ios>
#include <optional>
#include <sstream>
#include <vector>

#include <core/json/JsonParser.h>
#include <core/json/JsonValue.h>
#include <ecs/ArchetypeSignature.h>
#include <ecs/World.h>
#include <math/geometry/3d/Transform3d.h>
#include <physics/CollisionShapeCache.h>
#include <physics/components/Collider.h>
#include <world/transform/TransformComponents.h>

namespace
{
std::vector<std::byte> ReadFileBytes(const std::string& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return {};

    const std::streamsize size = file.tellg();
    if (size <= 0)
        return {};

    file.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size))
        return {};
    return bytes;
}

Vec3d ReadOrigin(const JsonValue& entry)
{
    const JsonValue* origin = entry.Find("origin");
    if (origin == nullptr || !origin->IsArray()
        || origin->AsArray().size() != 3)
    {
        return Vec3d::Zero();
    }

    const JsonValue::Array& values = origin->AsArray();
    return Vec3d(
        static_cast<float>(
            values[0].IsNumber() ? values[0].AsNumber() : 0.0),
        static_cast<float>(
            values[1].IsNumber() ? values[1].AsNumber() : 0.0),
        static_cast<float>(
            values[2].IsNumber() ? values[2].AsNumber() : 0.0));
}
} // namespace

int LoadZoneCollision(
    World& world,
    CollisionShapeCache& cache,
    const std::string& sidecarPath,
    const std::string& cookedRoot,
    StoragePartitionId partition)
{
    std::ifstream file(sidecarPath);
    if (!file.is_open())
        return 0;

    std::ostringstream buffer;
    buffer << file.rdbuf();

    JsonParseError parseError;
    std::optional<JsonValue> json =
        JsonParse(buffer.str(), &parseError);
    if (!json || !json->IsArray())
        return 0;

    ArchetypeSignature colliderSignature;
    colliderSignature.set(world.GetComponentId<LocalTransform>());
    colliderSignature.set(world.GetComponentId<Collider>());

    int loaded = 0;
    for (const JsonValue& entry : json->AsArray())
    {
        if (!entry.IsObject())
            continue;

        const JsonValue* blob = entry.Find("blob");
        if (blob == nullptr || !blob->IsString())
            continue;

        const std::vector<std::byte> bytes = ReadFileBytes(
            cookedRoot + "/" + blob->AsString());
        if (bytes.empty())
            continue;

        const CollisionShapeHandle handle = cache.LoadBlob(bytes);
        if (!handle.IsValid())
            continue;

        Transform3f transform;
        transform.Position = ReadOrigin(entry);

        const EntityId entity = world.CreateEntityWithSignature(
            partition,
            colliderSignature);
        *world.TryGet<LocalTransform>(entity) =
            LocalTransform{ transform };
        Collider collider;
        collider.Mesh = handle;
        *world.TryGet<Collider>(entity) = collider;
        ++loaded;
    }
    return loaded;
}
