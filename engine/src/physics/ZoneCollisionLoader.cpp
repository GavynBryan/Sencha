#include <physics/ZoneCollisionLoader.h>

#include <cstddef>
#include <cstring>
#include <fstream>
#include <optional>
#include <sstream>
#include <vector>

#include <core/json/JsonParser.h>
#include <core/json/JsonValue.h>
#include <ecs/World.h>
#include <math/geometry/3d/Transform3d.h>
#include <physics/CollisionShapeCache.h>
#include <physics/components/Collider.h>
#include <world/transform/TransformComponents.h>

namespace
{
std::vector<std::byte> ReadFileBytes(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
        return {};
    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return bytes;
}

Vec3d ReadOrigin(const JsonValue& entry)
{
    const JsonValue* origin = entry.Find("origin");
    if (origin == nullptr || !origin->IsArray() || origin->AsArray().size() != 3)
        return Vec3d::Zero();
    const JsonValue::Array& a = origin->AsArray();
    return Vec3d(
        static_cast<float>(a[0].IsNumber() ? a[0].AsNumber() : 0.0),
        static_cast<float>(a[1].IsNumber() ? a[1].AsNumber() : 0.0),
        static_cast<float>(a[2].IsNumber() ? a[2].AsNumber() : 0.0));
}
} // namespace

int LoadZoneCollision(World& world,
                      CollisionShapeCache& cache,
                      const std::string& sidecarPath,
                      const std::string& cookedRoot)
{
    std::ifstream file(sidecarPath);
    if (!file.is_open())
        return 0; // no sidecar: a level with no brush collision

    std::ostringstream buffer;
    buffer << file.rdbuf();

    JsonParseError parseError;
    std::optional<JsonValue> json = JsonParse(buffer.str(), &parseError);
    if (!json || !json->IsArray())
        return 0;

    int loaded = 0;
    for (const JsonValue& entry : json->AsArray())
    {
        if (!entry.IsObject())
            continue;
        const JsonValue* blob = entry.Find("blob");
        if (blob == nullptr || !blob->IsString())
            continue;

        const std::vector<std::byte> bytes = ReadFileBytes(cookedRoot + "/" + blob->AsString());
        if (bytes.empty())
            continue;
        const CollisionShapeHandle handle = cache.LoadBlob(bytes);
        if (!handle.IsValid())
            continue;

        Transform3f transform;
        transform.Position = ReadOrigin(entry);

        const EntityId entity = world.CreateEntity();
        world.AddComponent<LocalTransform>(entity, LocalTransform{ transform });
        Collider collider;
        collider.Mesh = handle;
        world.AddComponent<Collider>(entity, collider);
        ++loaded;
    }
    return loaded;
}
