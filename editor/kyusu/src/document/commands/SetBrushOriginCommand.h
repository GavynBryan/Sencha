#pragma once

#include "commands/ICommand.h"
#include "brush/BrushMesh.h"
#include "document/EditorScene.h"

#include <math/geometry/3d/Transform3d.h>

#include <cmath>
#include <memory>
#include <utility>

// Moves an entity's origin (transform position) to a new world point and shifts
// the brush's local vertices by the inverse, so the geometry stays exactly where
// it was. One undoable step (transform + mesh together).
class SetBrushOriginCommand : public ICommand
{
public:
    SetBrushOriginCommand(EditorScene& scene, EntityId entity,
                          Transform3f beforeTransform, Transform3f afterTransform,
                          BrushMesh beforeMesh, BrushMesh afterMesh)
        : Scene(scene), Entity(entity)
        , BeforeTransform(beforeTransform), AfterTransform(afterTransform)
        , BeforeMesh(std::move(beforeMesh)), AfterMesh(std::move(afterMesh)) {}

    void Execute() override
    {
        Scene.SetTransform(Entity, AfterTransform);
        Scene.SetBrushMesh(Entity, AfterMesh);
    }

    void Undo() override
    {
        Scene.SetTransform(Entity, BeforeTransform);
        Scene.SetBrushMesh(Entity, BeforeMesh);
    }

private:
    EditorScene& Scene;
    EntityId Entity;
    Transform3f BeforeTransform;
    Transform3f AfterTransform;
    BrushMesh BeforeMesh;
    BrushMesh AfterMesh;
};

// Builds the command that re-origins `entity` to `newOrigin` (world). nullptr if
// the entity has no brush/transform, or the origin is already there (no-op).
[[nodiscard]] inline std::unique_ptr<ICommand> MakeSetBrushOriginCommand(EditorScene& scene,
                                                                         EntityId entity,
                                                                         Vec3d newOrigin)
{
    const Transform3f* transform = scene.TryGetTransform(entity);
    const BrushMesh* mesh = scene.TryGetBrushMesh(entity);
    if (transform == nullptr || mesh == nullptr)
        return nullptr;

    const Vec3d worldShift = transform->Position - newOrigin;
    if (worldShift.SqrMagnitude() <= 1.0e-10f)
        return nullptr;

    // Keep world positions fixed: newLocal = oldLocal + (R*S)^-1 * (oldOrigin - newOrigin).
    const Vec3d unrotated = transform->Rotation.Conjugate().RotateVector(worldShift);
    const Vec3d localShift(
        transform->Scale.X != 0.0f ? unrotated.X / transform->Scale.X : unrotated.X,
        transform->Scale.Y != 0.0f ? unrotated.Y / transform->Scale.Y : unrotated.Y,
        transform->Scale.Z != 0.0f ? unrotated.Z / transform->Scale.Z : unrotated.Z);

    Transform3f afterTransform = *transform;
    afterTransform.Position = newOrigin;

    BrushMesh afterMesh = *mesh;
    for (BrushVertex& vertex : afterMesh.Vertices)
        vertex.Position += localShift;

    return std::make_unique<SetBrushOriginCommand>(
        scene, entity, *transform, afterTransform, *mesh, std::move(afterMesh));
}
