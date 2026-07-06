#include "FaceMaterialEdits.h"

#include "brush/BrushMesh.h"
#include "commands/CommandStack.h"
#include "meshedit/IMeshEditTarget.h"
#include "selection/SelectionService.h"

#include <ecs/EntityId.h>

#include <algorithm>
#include <span>
#include <utility>

namespace
{
    // Newell's method over world-space corners: direction is the face normal,
    // magnitude twice the area, so summing across faces area-weights the average.
    Vec3d WorldFaceAreaNormal(std::span<const Vec3d> corners)
    {
        Vec3d normal{ 0.0f, 0.0f, 0.0f };
        const std::size_t n = corners.size();
        for (std::size_t i = 0; i < n; ++i)
        {
            const Vec3d& a = corners[i];
            const Vec3d& b = corners[(i + 1) % n];
            normal.X += (a.Y - b.Y) * (a.Z + b.Z);
            normal.Y += (a.Z - b.Z) * (a.X + b.X);
            normal.Z += (a.X - b.X) * (a.Y + b.Y);
        }
        return normal;
    }
}

std::vector<Vec3d> FaceLocalPositions(const BrushMesh& mesh, const BrushFace& face)
{
    std::vector<Vec3d> positions;
    positions.reserve(face.Loop.size());
    for (std::uint32_t index : face.Loop)
        positions.push_back(mesh.Vertices[index].Position);
    return positions;
}

void EditSelectedFaces(
    IMeshEditTarget& target,
    const SelectionService& selection,
    CommandStack& commands,
    const std::function<void(const BrushMesh&, const Transform3f&, BrushFace&)>& mutate)
{
    // Group selected face indices by entity so a multi-face edit on one brush is
    // a single command (value-semantics before/after).
    std::vector<std::pair<EntityId, std::vector<std::uint32_t>>> byEntity;
    for (const SelectableRef& ref : selection.GetSelection())
    {
        if (!ref.IsFace())
            continue;
        auto it = std::find_if(byEntity.begin(), byEntity.end(),
                               [&](const auto& e) { return e.first == ref.Entity; });
        if (it == byEntity.end())
            byEntity.push_back({ ref.Entity, { ref.ElementId } });
        else
            it->second.push_back(ref.ElementId);
    }

    for (auto& [entity, faces] : byEntity)
    {
        const std::optional<MeshEditTargetMesh> resolved = target.Resolve(entity);
        if (!resolved || !resolved->Mesh)
            continue;

        BrushMesh before = *resolved->Mesh;
        BrushMesh after = before;
        bool changed = false;
        for (std::uint32_t f : faces)
        {
            if (f < after.Faces.size())
            {
                mutate(after, resolved->Transform, after.Faces[f]);
                changed = true;
            }
        }
        if (changed)
        {
            if (auto command = target.MakeEditCommand(entity, std::move(before), std::move(after)))
                commands.Execute(std::move(command));
        }
    }
}

void ApplyMaterialToSelectedFaces(IMeshEditTarget& target,
                                  const SelectionService& selection,
                                  CommandStack& commands,
                                  const AssetRef& material)
{
    EditSelectedFaces(target, selection, commands,
                      [&](const BrushMesh&, const Transform3f&, BrushFace& face) {
                          face.Material.Material = material;
                      });
}

std::optional<UvProjection> RepresentativeFaceUv(const IMeshEditTarget& target,
                                                 const SelectionService& selection)
{
    for (const SelectableRef& ref : selection.GetSelection())
    {
        if (!ref.IsFace())
            continue;
        const std::optional<MeshEditTargetMesh> resolved = target.Resolve(ref.Entity);
        if (resolved && resolved->Mesh && ref.ElementId < resolved->Mesh->Faces.size())
            return resolved->Mesh->Faces[ref.ElementId].Material.Uv;
    }
    return std::nullopt;
}

std::optional<FaceMaterialClipboard> CopySelectedFaceProjection(const IMeshEditTarget& target,
                                                                const SelectionService& selection)
{
    const SelectableRef primary = selection.GetPrimarySelection();
    SelectableRef source = primary.IsFace() ? primary : SelectableRef{};
    if (!source.IsValid())
    {
        for (const SelectableRef& ref : selection.GetSelection())
            if (ref.IsFace())
            {
                source = ref;
                break;
            }
    }
    if (!source.IsValid())
        return std::nullopt;

    const std::optional<MeshEditTargetMesh> resolved = target.Resolve(source.Entity);
    if (!resolved || !resolved->Mesh || source.ElementId >= resolved->Mesh->Faces.size())
        return std::nullopt;

    const FaceMaterial& face = resolved->Mesh->Faces[source.ElementId].Material;
    return FaceMaterialClipboard{
        .Projection = UvProjectionToWorld(face.Uv, resolved->Transform),
        .Material = face.Material,
    };
}

void PasteFaceProjection(IMeshEditTarget& target,
                         const SelectionService& selection,
                         CommandStack& commands,
                         const FaceMaterialClipboard& clipboard)
{
    EditSelectedFaces(target, selection, commands,
                      [&](const BrushMesh&, const Transform3f& transform, BrushFace& f) {
                          f.Material.Material = clipboard.Material;
                          f.Material.Uv = UvProjectionToLocal(clipboard.Projection, transform);
                      });
}

void JustifySelectedFacesAsOne(IMeshEditTarget& target,
                               const SelectionService& selection,
                               CommandStack& commands,
                               bool fit)
{
    // Union pass: every selected face's world loop positions and the
    // area-weighted average world normal.
    std::vector<Vec3d> worldPositions;
    std::vector<Vec3d> corners;
    Vec3d normalSum{ 0.0f, 0.0f, 0.0f };
    for (const SelectableRef& ref : selection.GetSelection())
    {
        if (!ref.IsFace())
            continue;
        const std::optional<MeshEditTargetMesh> resolved = target.Resolve(ref.Entity);
        if (!resolved || !resolved->Mesh || ref.ElementId >= resolved->Mesh->Faces.size())
            continue;
        const BrushFace& face = resolved->Mesh->Faces[ref.ElementId];
        corners.clear();
        for (std::uint32_t index : face.Loop)
            corners.push_back(resolved->Transform.TransformPoint(resolved->Mesh->Vertices[index].Position));
        normalSum = normalSum + WorldFaceAreaNormal(corners);
        worldPositions.insert(worldPositions.end(), corners.begin(), corners.end());
    }
    if (worldPositions.empty())
        return;
    if (normalSum.SqrMagnitude() <= 0.0f)
        normalSum = { 0.0f, 1.0f, 0.0f };

    // One world projection for the whole selection: box-mapping axes from the
    // dominant axis of the average normal (UvProjectionForNormal's world-aligned
    // axes ARE world axes), density seeded from the representative face so
    // Center keeps the current tiling.
    const UvProjection axes = UvProjectionForNormal(normalSum, /*worldAligned*/ true);
    WorldUvProjection world;
    world.AxisU = axes.AxisU;
    world.AxisV = axes.AxisV;
    if (const std::optional<UvProjection> representative = RepresentativeFaceUv(target, selection))
        world.Scale = representative->Scale;
    world = fit ? WorldUvProjectionFit(world, worldPositions)
                : WorldUvProjectionCenter(world, worldPositions);

    // Every face takes the SAME world mapping, baked into its brush's frame, so
    // the texture flows continuously across faces and across brushes.
    EditSelectedFaces(target, selection, commands,
                      [&](const BrushMesh&, const Transform3f& transform, BrushFace& f) {
                          f.Material.Uv = UvProjectionToLocal(world, transform);
                      });
}
