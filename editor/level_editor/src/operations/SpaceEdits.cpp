#include "SpaceEdits.h"

#include "LevelSubjects.h"
#include "SpaceRelations.h"

#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/EntityChangeSet.h"
#include "document/SpaceComponents.h"
#include "document/commands/RenameEntityCommand.h"
#include "meshedit/MeshEditService.h"

#include <world/registry/Registry.h>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float kSmallest = 0.1f;

    // Floors take the floor material, ceilings the ceiling's, the rest the walls'.
    void PaintByConvention(BrushMesh& mesh, const LevelConventions& conventions)
    {
        for (BrushFace& face : mesh.Faces)
        {
            const float up = BrushComputeFaceNormal(mesh, face).Y;
            const std::optional<AssetRef>& material = up < -0.7f ? conventions.FloorMaterial
                : up > 0.7f                                       ? conventions.CeilingMaterial
                                                                  : conventions.WallMaterial;
            if (material)
                face.Material.Material = *material;
        }
    }

    bool OnlyMoves(const Transform3f& before, const Transform3f& after)
    {
        const auto same = [](const auto& a, const auto& b) {
            return std::abs(a.X - b.X) < 1e-5f && std::abs(a.Y - b.Y) < 1e-5f && std::abs(a.Z - b.Z) < 1e-5f;
        };
        return same(before.Scale, after.Scale) && std::abs(before.Rotation.Dot(after.Rotation)) > 1.0f - 1e-6f;
    }

    struct Pull
    {
        FarSide Far;
        Vec3d   Offset;
    };
}

LevelConventions ConventionsOf(const EditorDocument& document)
{
    return LevelConventionsFrom(FindLevelConventions(document.ContentRoots()).Values);
}

EditorOperationOutcome CreateSpace(EditorDocument& document, EntityChangeRecorder& recorder, const NewSpace& space,
                                   EntityId& created)
{
    if (space.Name.empty())
        return EditorOperationOutcome::Refused("space.name_invalid", "a space needs a name");
    if (ResolveEntitySubject(document, "@" + space.Name).Resolved())
        return EditorOperationOutcome::Refused("space.name_taken", "something is already called '" + space.Name + "'");
    const Vec3d size = space.Max - space.Min;
    if (size.X < kSmallest || size.Y < kSmallest || size.Z < kSmallest)
        return EditorOperationOutcome::Refused("space.too_small", "max must exceed min by at least 0.1 m on every axis");

    const LevelConventions conventions = ConventionsOf(document);
    EditorScene& scene = document.GetScene();
    created = scene.CreateBrush((space.Min + space.Max) * 0.5f, size * 0.5f);
    BrushMesh mesh = *scene.TryGetBrushMesh(created);
    PaintByConvention(mesh, conventions);
    scene.SetBrushMesh(created, std::move(mesh));
    World& components = scene.GetRegistry().Components;
    components.AddComponent(created, BrushVolume{});
    components.AddComponent(created, Space{});
    if (space.Shelled)
        components.AddComponent(created, Shell{ .Thickness = space.Thickness.value_or(conventions.ShellThickness) });
    if (auto rename = MakeRenameEntityCommand(created, space.Name, scene, document))
        rename->Execute();
    recorder.Created(created);
    document.MarkDirty();
    return EditorOperationOutcome::Ok({});
}

EditorOperationOutcome MoveEntities(EditorDocument& document, EntityChangeRecorder& recorder,
                                    std::span<const EntityMove> moves, bool carry, MoveReport& report)
{
    EditorScene& scene = document.GetScene();
    const World& components = scene.GetRegistry().Components;
    const auto moving = [&](EntityId entity) {
        return std::ranges::any_of(moves, [&](const EntityMove& move) { return move.Entity == entity; });
    };

    // The far sides are found before anything moves, where the doors still are.
    std::vector<Pull> pulls;
    if (carry)
        for (const EntityMove& move : moves)
        {
            const Transform3f* before = scene.TryGetWorldTransform(move.Entity);
            if (before == nullptr || !components.HasComponent<Space>(move.Entity) || !OnlyMoves(*before, move.World))
                continue;
            for (const EntityId opening : scene.GetAllEntities())
            {
                if (!components.HasComponent<Opening>(opening) || OwnerOf(scene, opening) != move.Entity)
                    continue;
                const std::optional<FarSide> far = FarSideOf(scene, move.Entity, opening);
                if (!far || moving(far->Space)
                    || std::ranges::any_of(pulls, [&](const Pull& seen) {
                           return seen.Far.Space == far->Space && seen.Far.Face == far->Face;
                       }))
                    continue;
                pulls.push_back({ *far, move.World.Position - before->Position });
            }
        }

    for (const EntityMove& move : moves)
    {
        recorder.Touch(move.Entity);
        scene.SetWorldTransform(move.Entity, move.World);
    }
    for (const Pull& pull : pulls)
    {
        if (!components.HasComponent<Shell>(pull.Far.Space))
        {
            report.Skipped.push_back(pull.Far.Space);
            continue;
        }
        const Vec3d delta = pull.Far.Outward * pull.Offset.Dot(pull.Far.Outward);
        if (delta.Magnitude() < 1e-4f)
            continue;
        recorder.Touch(pull.Far.Space);
        if (!MoveFace(scene, pull.Far.Space, pull.Far.Face, delta))
            return EditorOperationOutcome::Refused("space.carry_invalid",
                                                   "carrying would turn " + EntityNameText(document, pull.Far.Space)
                                                       + " inside out; move it less, or without carry");
        report.Carried.push_back(pull.Far.Space);
    }
    document.MarkDirty();
    return EditorOperationOutcome::Ok({});
}

bool MoveFace(EditorScene& scene, EntityId entity, std::uint32_t face, const Vec3d& delta)
{
    const BrushMesh& mesh = *scene.TryGetBrushMesh(entity);
    const Transform3f& world = *scene.TryGetWorldTransform(entity);
    const SelectableRef ref = SelectableRef::FaceSelection(scene.GetRegistry().Id, entity, face);
    std::optional<BrushMesh> moved =
        MeshEditService{}.TranslateElements(mesh, world, std::span(&ref, 1), MeshElementKind::Face, delta, true);
    if (!moved || moved->Faces.size() != mesh.Faces.size())
        return false;
    for (std::size_t f = 0; f < mesh.Faces.size(); ++f)
        if (moved->Faces[f].Normal.Dot(mesh.Faces[f].Normal) <= 0.0f)
            return false;
    scene.SetBrushMesh(entity, std::move(*moved));
    return true;
}
