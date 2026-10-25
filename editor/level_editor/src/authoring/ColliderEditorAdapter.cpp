#include "authoring/ColliderEditorAdapter.h"

#include "EditorTheme.h"
#include "authoring/AffordanceStrokes.h"
#include "commands/CommandStack.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/commands/ValueCommand.h"
#include "properties/SettingProperties.h"

#include <ecs/World.h>
#include <physics/CollisionShape.h>
#include <physics/RigidBodyBinding.h>
#include <physics/components/Collider.h>
#include <render/StaticMeshComponent.h>
#include <render/static_mesh/GpuStaticMesh.h>
#include <render/static_mesh/StaticMeshCache.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <format>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

namespace
{
// Listed in the order a level designer reaches for them.
constexpr std::array<std::string_view, 3> kShapes = { "Box", "Sphere", "Capsule" };
constexpr std::array<CollisionShapeType, 3> kShapeTypes = { CollisionShapeType::Box, CollisionShapeType::Sphere,
                                                            CollisionShapeType::Capsule };
constexpr float kSmallest = 0.001f;

std::size_t ShapeIndex(CollisionShapeType type)
{
    return static_cast<std::size_t>(std::ranges::find(kShapeTypes, type) - kShapeTypes.begin());
}

const Collider* ColliderOf(const EditorScene& scene, EntityId entity)
{
    const World& world = scene.GetRegistry().Components;
    return world.IsRegistered<Collider>() ? world.TryGet<Collider>(entity) : nullptr;
}

std::array<double, 3> Doubles(const Vec3d& value)
{
    return { value.X, value.Y, value.Z };
}

std::optional<Vec3d> ParseVec3(std::string_view value)
{
    const std::optional<std::vector<double>> parts = ParseVector(value, 3);
    if (!parts)
        return std::nullopt;
    return Vec3d(static_cast<float>((*parts)[0]), static_cast<float>((*parts)[1]), static_cast<float>((*parts)[2]));
}

std::string VectorText(const Vec3d& value)
{
    return std::format("{}, {}, {}", NumberText(value.X), NumberText(value.Y), NumberText(value.Z));
}

// The shape sized to `bounds`, keeping its kind: a box fills them, a sphere
// reaches their widest half, a capsule stands in them upright.
CollisionShape FittedShape(CollisionShapeType type, const Aabb3d& bounds)
{
    const Vec3d half = bounds.HalfExtent();
    CollisionShape shape;
    switch (type)
    {
    case CollisionShapeType::Box:
        shape = CollisionShape::MakeBox(Vec3d(std::max(half.X, kSmallest), std::max(half.Y, kSmallest),
                                              std::max(half.Z, kSmallest)));
        break;
    case CollisionShapeType::Sphere:
        shape = CollisionShape::MakeSphere(std::max({ half.X, half.Y, half.Z, kSmallest }));
        break;
    case CollisionShapeType::Capsule:
    {
        const float radius = std::max({ half.X, half.Z, kSmallest });
        shape = CollisionShape::MakeCapsule(radius, std::max(half.Y - radius, 0.0f));
        break;
    }
    }
    shape.Center = bounds.Center();
    return shape;
}

// The same room in another kind, when the kind is switched by hand.
CollisionShape Reshaped(const CollisionShape& shape, CollisionShapeType type)
{
    Vec3d half = shape.HalfExtents;
    if (shape.Type == CollisionShapeType::Sphere)
        half = Vec3d(shape.Radius, shape.Radius, shape.Radius);
    else if (shape.Type == CollisionShapeType::Capsule)
        half = Vec3d(shape.Radius, shape.HalfHeight + shape.Radius, shape.Radius);
    CollisionShape reshaped = FittedShape(type, Aabb3d::FromCenterHalfExtent(shape.Center, half));
    reshaped.Orientation = shape.Orientation;
    return reshaped;
}

void AppendArc(ViewportAffordanceOutput& output, Vec3d centre, Vec3d u, Vec3d v, float radius, float from,
               float to, Vec4 colour)
{
    constexpr int kSegments = 32;
    const int steps = std::max(2, static_cast<int>(std::ceil(kSegments * (to - from) / (2.0f * std::numbers::pi_v<float>))));
    Vec3d last = centre + (u * std::cos(from) + v * std::sin(from)) * radius;
    for (int i = 1; i <= steps; ++i)
    {
        const float angle = from + (to - from) * static_cast<float>(i) / static_cast<float>(steps);
        const Vec3d next = centre + (u * std::cos(angle) + v * std::sin(angle)) * radius;
        AppendLine(output, last, next, colour);
        last = next;
    }
}

// Where the body holds the shape: the entity's position and turn, and the
// shape grown by the entity's scale exactly as the runtime grows it.
void DrawShape(const Transform3f& entity, const Collider& collider, Vec4 colour, ViewportAffordanceOutput& output)
{
    const CollisionShape shape = ScaledCollisionShape(collider.Shape, entity.Scale);
    const Quatf turn = entity.Rotation * shape.Orientation;
    const Vec3d centre = entity.Position + entity.Rotation.RotateVector(shape.Center);
    const Vec3d x = turn.RotateVector(Vec3d(1.0f, 0.0f, 0.0f));
    const Vec3d y = turn.RotateVector(Vec3d(0.0f, 1.0f, 0.0f));
    const Vec3d z = turn.RotateVector(Vec3d(0.0f, 0.0f, 1.0f));
    constexpr float kPi = std::numbers::pi_v<float>;
    switch (shape.Type)
    {
    case CollisionShapeType::Box:
    {
        const Vec3d h = shape.HalfExtents;
        const auto corner = [&](int i) {
            return centre + x * ((i & 1) ? h.X : -h.X) + y * ((i & 2) ? h.Y : -h.Y) + z * ((i & 4) ? h.Z : -h.Z);
        };
        for (int i = 0; i < 8; ++i)
            for (int bit : { 1, 2, 4 })
                if ((i & bit) == 0)
                    AppendLine(output, corner(i), corner(i | bit), colour);
        break;
    }
    case CollisionShapeType::Sphere:
        AppendArc(output, centre, x, y, shape.Radius, 0.0f, 2.0f * kPi, colour);
        AppendArc(output, centre, y, z, shape.Radius, 0.0f, 2.0f * kPi, colour);
        AppendArc(output, centre, z, x, shape.Radius, 0.0f, 2.0f * kPi, colour);
        break;
    case CollisionShapeType::Capsule:
    {
        const Vec3d top = centre + y * shape.HalfHeight;
        const Vec3d bottom = centre - y * shape.HalfHeight;
        AppendArc(output, top, x, z, shape.Radius, 0.0f, 2.0f * kPi, colour);
        AppendArc(output, bottom, x, z, shape.Radius, 0.0f, 2.0f * kPi, colour);
        for (const Vec3d side : { x, -x, z, -z })
            AppendLine(output, top + side * shape.Radius, bottom + side * shape.Radius, colour);
        AppendArc(output, top, x, y, shape.Radius, 0.0f, kPi, colour);
        AppendArc(output, top, z, y, shape.Radius, 0.0f, kPi, colour);
        AppendArc(output, bottom, x, y, shape.Radius, kPi, 2.0f * kPi, colour);
        AppendArc(output, bottom, z, y, shape.Radius, kPi, 2.0f * kPi, colour);
        break;
    }
    }
}

void Apply(EditorComponentContext& context, const Collider& collider)
{
    context.Scene.SetComponent(context.Entity, collider);
}

// A box's handles: the box in its own frame, placed by the entity (its scale
// included) and the shape's centre and turn, so a handle tracks the face it
// stands on and a drag of one face keeps the opposite one where it was.
class ColliderBoxTransaction final : public IValueEditTransaction<Aabb3d>
{
public:
    ColliderBoxTransaction(EditorComponentContext context, Collider before)
        : Context(context), Before(before) {}

    void Preview(const Aabb3d& value) override { Apply(Context, After(value)); }

    void Commit(const Aabb3d& value) override
    {
        const Collider after = After(value);
        Apply(Context, Before);
        if (after.Shape == Before.Shape)
            return;
        Context.Commands.Execute(std::make_unique<ValueCommand<Collider>>(
            Before, after,
            [&scene = Context.Scene, entity = Context.Entity](const Collider& collider) {
                scene.SetComponent(entity, collider);
            },
            Context.Document));
    }

    void Cancel() override { Apply(Context, Before); }

private:
    [[nodiscard]] Collider After(const Aabb3d& value) const
    {
        Collider after = Before;
        const Vec3d half = value.HalfExtent();
        after.Shape.HalfExtents = Vec3d(std::max(half.X, kSmallest), std::max(half.Y, kSmallest),
                                        std::max(half.Z, kSmallest));
        after.Shape.Center = Before.Shape.Center + Before.Shape.Orientation.RotateVector(value.Center());
        return after;
    }

    EditorComponentContext Context;
    Collider Before;
};

class ColliderEditorAdapter final : public IEditorComponentAdapter
{
public:
    explicit ColliderEditorAdapter(ColliderMeshBounds meshBounds) : MeshBounds(std::move(meshBounds)) {}

    [[nodiscard]] ComponentTypeId Type() const override { return ResolveComponentTypeId<Collider>(); }

    bool AuthorsViewportAffordances() const override { return true; }

    void BuildViewport(const EditorComponentContext& context, ViewportAffordanceOutput& output) const override
    {
        const Collider* collider = ColliderOf(context.Scene, context.Entity);
        // A cooked mesh shape is the brush cook's, never authored here.
        if (collider == nullptr || collider->Mesh.IsValid())
            return;
        Vec4 colour = collider->IsTrigger ? EditorTheme::VolumeEdge : EditorTheme::ColliderEdge;
        if (!context.Selected)
            colour.W = 0.45f;
        DrawShape(context.Transform, *collider, colour, output);
        if (!context.Selected || collider->Shape.Type != CollisionShapeType::Box)
            return;

        const CollisionShape& shape = collider->Shape;
        const Transform3f frame(context.Transform.TransformPoint(shape.Center),
                                context.Transform.Rotation * shape.Orientation, context.Transform.Scale);
        output.Aabbs.push_back(AabbEditTarget{
            .Key = PackEntity(context.Entity),
            .LocalToWorld = frame,
            .Value = Aabb3d::FromCenterHalfExtent(Vec3d::Zero(), shape.HalfExtents),
            .BeginEdit = [context, before = *collider] {
                return std::make_unique<ColliderBoxTransaction>(context, before);
            },
        });
    }

    bool AuthorsInspectorRows() const override { return true; }

    std::vector<PropertyNode> InspectorNodes(const EditorComponentInspectorContext& context) const override
    {
        const Collider* collider = ColliderOf(context.Scene, context.Entity);
        if (collider == nullptr)
            return {};
        const CollisionShape& shape = collider->Shape;
        std::vector<PropertyNode> nodes;

        PropertyNode kind = ChoiceProperty("shape", "Shape", kShapes, ShapeIndex(shape.Type),
                                           "What the body collides as. Sizes are in the entity's own frame: "
                                           "the collider scales with the entity, as its mesh does.");
        if (MeshBounds(context.Document, context.Entity).has_value())
            kind.Descriptor.Verbs.push_back({ .Id = "fit", .Label = "Fit to Mesh" });
        nodes.push_back(std::move(kind));

        switch (shape.Type)
        {
        case CollisionShapeType::Box:
        {
            const std::array<double, 3> size = Doubles(shape.HalfExtents * 2.0f);
            nodes.push_back(VectorProperty("size", "Size", size, "Width, height and depth, before the entity's scale."));
            break;
        }
        case CollisionShapeType::Sphere:
            nodes.push_back(NumberProperty("radius", "Radius", shape.Radius, kSmallest, -1.0, 0.05));
            break;
        case CollisionShapeType::Capsule:
            nodes.push_back(NumberProperty("radius", "Radius", shape.Radius, kSmallest, -1.0, 0.05));
            nodes.push_back(NumberProperty("height", "Height", 2.0 * (shape.HalfHeight + shape.Radius), kSmallest,
                                           -1.0, 0.05, "End to end, caps included, along the entity's up."));
            break;
        }
        const std::array<double, 3> offset = Doubles(shape.Center);
        nodes.push_back(VectorProperty("offset", "Offset", offset, "The shape's centre from the entity's origin."));
        if (shape.Orientation != Quatf::Identity())
        {
            PropertyNode turned = NoteProperty("turned", "Turned", "Against the entity, as a cook fitted it.");
            turned.Descriptor.Verbs.push_back({ .Id = "straighten", .Label = "Straighten" });
            nodes.push_back(std::move(turned));
        }
        nodes.push_back(ToggleProperty("sensor", "Sensor", collider->IsTrigger,
                                       "Reports what overlaps it instead of blocking."));

        if (const Transform3f* transform = context.Scene.TryGetWorldTransform(context.Entity);
            transform != nullptr && transform->Scale != Vec3d::One())
            nodes.push_back(NoteProperty("scaled", "In the level", ScaledText(*transform, shape)));
        return nodes;
    }

    PropertyEditResult SetInspectorValue(EditorComponentInspectorContext& context, std::string_view key,
                                         std::string_view value) const override
    {
        const Collider* current = ColliderOf(context.Scene, context.Entity);
        if (current == nullptr)
            return PropertyEditResult::Gone;
        Collider after = *current;
        CollisionShape& shape = after.Shape;
        if (key == "shape")
        {
            const std::optional<std::size_t> index = ParseChoice(value, kShapes);
            if (!index)
                return PropertyEditResult::Refused;
            if (kShapeTypes[*index] != shape.Type)
                shape = Reshaped(shape, kShapeTypes[*index]);
        }
        else if (key == "size" && shape.Type == CollisionShapeType::Box)
        {
            const std::optional<Vec3d> size = ParseVec3(value);
            if (!size || size->X <= 0.0f || size->Y <= 0.0f || size->Z <= 0.0f)
                return PropertyEditResult::Refused;
            shape.HalfExtents = *size * 0.5f;
        }
        else if (key == "radius" && shape.Type != CollisionShapeType::Box)
        {
            const std::optional<double> radius = ParseNumber(value, kSmallest);
            if (!radius)
                return PropertyEditResult::Refused;
            // A capsule keeps its height: the caps grow into the middle.
            const float height = shape.HalfHeight + shape.Radius;
            shape.Radius = static_cast<float>(*radius);
            if (shape.Type == CollisionShapeType::Capsule)
                shape.HalfHeight = std::max(height - shape.Radius, 0.0f);
        }
        else if (key == "height" && shape.Type == CollisionShapeType::Capsule)
        {
            const std::optional<double> height = ParseNumber(value, kSmallest);
            if (!height)
                return PropertyEditResult::Refused;
            shape.HalfHeight = std::max(static_cast<float>(*height) * 0.5f - shape.Radius, 0.0f);
        }
        else if (key == "offset")
        {
            const std::optional<Vec3d> offset = ParseVec3(value);
            if (!offset)
                return PropertyEditResult::Refused;
            shape.Center = *offset;
        }
        else if (key == "sensor")
        {
            const std::optional<bool> on = ParseToggle(value);
            if (!on)
                return PropertyEditResult::Refused;
            after.IsTrigger = *on;
        }
        else
            return PropertyEditResult::Gone;
        return Commit(context, *current, after);
    }

    PropertyEditResult ActInspector(EditorComponentInspectorContext& context, std::string_view key,
                                    std::string_view verb, std::string_view /*choice*/) const override
    {
        const Collider* current = ColliderOf(context.Scene, context.Entity);
        if (current == nullptr)
            return PropertyEditResult::Gone;
        Collider after = *current;
        if (key == "shape" && verb == "fit")
        {
            const std::optional<Aabb3d> bounds = MeshBounds(context.Document, context.Entity);
            if (!bounds)
                return PropertyEditResult::Refused;
            after.Shape = FittedShape(current->Shape.Type, *bounds);
        }
        else if (key == "turned" && verb == "straighten")
            after.Shape.Orientation = Quatf::Identity();
        else
            return PropertyEditResult::Gone;
        return Commit(context, *current, after);
    }

    // Added to an entity with a mesh, a collider starts as a box around it.
    void PrepareAdded(const EditorComponentInspectorContext& context, std::span<std::byte> added) const override
    {
        if (added.size() != sizeof(Collider))
            return;
        const std::optional<Aabb3d> bounds = MeshBounds(context.Document, context.Entity);
        if (!bounds)
            return;
        Collider collider;
        std::memcpy(&collider, added.data(), sizeof(Collider));
        collider.Shape = FittedShape(CollisionShapeType::Box, *bounds);
        std::memcpy(added.data(), &collider, sizeof(Collider));
    }

private:
    static std::string ScaledText(const Transform3f& transform, const CollisionShape& authored)
    {
        const CollisionShape shape = ScaledCollisionShape(authored, transform.Scale);
        switch (shape.Type)
        {
        case CollisionShapeType::Box:
            return std::format("{} m across", VectorText(shape.HalfExtents * 2.0f));
        case CollisionShapeType::Sphere:
            return std::format("radius {} m", NumberText(shape.Radius));
        case CollisionShapeType::Capsule:
            return std::format("radius {} m, {} m tall", NumberText(shape.Radius),
                               NumberText(2.0 * (shape.HalfHeight + shape.Radius)));
        }
        return {};
    }

    static PropertyEditResult Commit(EditorComponentInspectorContext& context, const Collider& before,
                                     const Collider& after)
    {
        if (after.Shape == before.Shape && after.IsTrigger == before.IsTrigger)
            return PropertyEditResult::Unchanged;
        context.Commands.Execute(std::make_unique<ValueCommand<Collider>>(
            before, after,
            [&scene = context.Scene, entity = context.Entity](const Collider& collider) {
                scene.SetComponent(entity, collider);
            },
            context.Document));
        return PropertyEditResult::Applied;
    }

    ColliderMeshBounds MeshBounds;
};
} // namespace

std::optional<Aabb3d> StaticMeshLocalBounds(const EditorDocument& document, EntityId entity)
{
    const StaticMeshCache* meshes = document.GetStaticMeshes();
    const World& world = document.GetScene().GetRegistry().Components;
    if (meshes == nullptr || !world.IsRegistered<StaticMeshComponent>())
        return std::nullopt;
    const StaticMeshComponent* component = world.TryGet<StaticMeshComponent>(entity);
    if (component == nullptr)
        return std::nullopt;
    const GpuStaticMesh* mesh = meshes->Get(component->Mesh);
    if (mesh == nullptr || !mesh->LocalBounds.IsValid())
        return std::nullopt;
    return mesh->LocalBounds;
}

std::unique_ptr<IEditorComponentAdapter> MakeColliderEditorAdapter(ColliderMeshBounds meshBounds)
{
    return std::make_unique<ColliderEditorAdapter>(std::move(meshBounds));
}
