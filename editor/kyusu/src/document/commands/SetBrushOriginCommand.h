#pragma once

#include "commands/ICommand.h"
#include "brush/BrushRecord.h"
#include "document/EditorScene.h"

#include <math/geometry/3d/Transform3d.h>

#include <cmath>
#include <memory>
#include <utility>

// Moves an entity's origin (transform position) to a new world point and shifts
// the brush's local vertices by the inverse, so the geometry stays exactly where
// it was. The modifier stack shifts with the vertices (a mirror plane is a
// local-space position too), so the evaluated result stays put as well. One
// undoable step (transform + record together).
class SetBrushOriginCommand : public ICommand
{
public:
    SetBrushOriginCommand(EditorScene& scene, EntityId entity,
                          Transform3f beforeTransform, Transform3f afterTransform,
                          BrushRecord before, BrushRecord after)
        : Scene(scene), Entity(entity)
        , BeforeTransform(beforeTransform), AfterTransform(afterTransform)
        , Before(std::move(before)), After(std::move(after)) {}

    void Execute() override
    {
        Scene.SetWorldTransform(Entity, AfterTransform);
        Scene.SetBrushRecord(Entity, After);
    }

    void Undo() override
    {
        Scene.SetWorldTransform(Entity, BeforeTransform);
        Scene.SetBrushRecord(Entity, Before);
    }

private:
    EditorScene& Scene;
    EntityId Entity;
    Transform3f BeforeTransform;
    Transform3f AfterTransform;
    BrushRecord Before;
    BrushRecord After;
};

// Builds the command that re-origins `entity` to `newOrigin` (world). nullptr if
// the entity has no brush/transform, or the origin is already there (no-op).
[[nodiscard]] inline std::unique_ptr<ICommand> MakeSetBrushOriginCommand(EditorScene& scene,
                                                                         EntityId entity,
                                                                         Vec3d newOrigin)
{
    const Transform3f* transform = scene.TryGetWorldTransform(entity);
    const BrushComponent* brush = scene.TryGetBrush(entity);
    const BrushRecord* record =
        brush != nullptr ? scene.GetBrushMeshStore().FindRecord(brush->Id) : nullptr;
    if (transform == nullptr || record == nullptr)
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

    BrushRecord after = *record;
    for (BrushVertex& vertex : after.Mesh.Vertices)
        vertex.Position += localShift;
    RebaseBrushModifiers(after.Modifiers, localShift);

    return std::make_unique<SetBrushOriginCommand>(
        scene, entity, *transform, afterTransform, *record, std::move(after));
}
