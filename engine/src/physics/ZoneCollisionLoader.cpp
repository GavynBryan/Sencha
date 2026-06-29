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
// One allocation, one read: size the buffer from the file length and read into
// it directly, rather than streaming through an ostringstream and a string.
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

    // Create collider entities directly in their final {LocalTransform, Collider}
    // archetype: one row allocation each, instead of an empty create plus two
    // add-component archetype moves per cell. The physics reconcile adds the
    // runtime PhysicsBodyLink later; the loader stays out of body lifetime.
    ArchetypeSignature colliderSig;
    colliderSig.set(world.GetComponentId<LocalTransform>());
    colliderSig.set(world.GetComponentId<Collider>());

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

        const EntityId entity = world.CreateEntityWithSignature(colliderSig);
        *world.TryGet<LocalTransform>(entity) = LocalTransform{ transform };
        Collider collider;
        collider.Mesh = handle;
        *world.TryGet<Collider>(entity) = collider;
        ++loaded;
    }
    return loaded;
}
