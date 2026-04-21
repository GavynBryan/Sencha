#pragma once

#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <math/MathSchemas.h>
#include <math/geometry/3d/Transform3d.h>
#include <render/Camera.h>
#include <world/SparseSetStore.h>
#include <world/entity/EntityId.h>
#include <world/registry/Registry.h>
#include <world/transform/TransformStore.h>

#include <span>
#include <string_view>
#include <vector>

struct CubePrimitive
{
    Vec3d HalfExtents = { 0.5, 0.5, 0.5 };
};

using CubePrimitiveStore = SparseSetStore<CubePrimitive>;

template <>
struct TypeSchema<CubePrimitive>
{
    static constexpr std::string_view Name = "cube_primitive";

    static auto Fields()
    {
        return std::tuple{
            MakeField("half_extents", &CubePrimitive::HalfExtents),
        };
    }
};

class LevelScene
{
public:
    explicit LevelScene(Registry& registry);

    EntityId CreateCube(Vec3d position, Vec3d halfExtents = { 0.5, 0.5, 0.5 });
    EntityId CreateCamera(Vec3d position);
    void DestroyEntity(EntityId entity);
    void SetTransform(EntityId entity, const Transform3f& transform);

    [[nodiscard]] bool HasEntity(EntityId entity) const;
    [[nodiscard]] uint32_t GetEntityCount() const;
    [[nodiscard]] std::span<const EntityId> GetAllEntities() const;
    [[nodiscard]] const Transform3f* TryGetTransform(EntityId entity) const;
    [[nodiscard]] const CubePrimitive* TryGetCube(EntityId entity) const;
    [[nodiscard]] const CameraComponent* TryGetCamera(EntityId entity) const;
    [[nodiscard]] Registry& GetRegistry();
    [[nodiscard]] const Registry& GetRegistry() const;

private:
    Registry& Registry_;
    std::vector<EntityId> Entities;
};
