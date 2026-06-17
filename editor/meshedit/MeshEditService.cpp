#include "MeshEditService.h"

#include "MeshElements.h"
#include "../level/brush/BrushOps.h"
#include "../level/brush/BrushValidation.h"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace
{
// A face's stable geometric identity (centroid + normal), used to re-find a
// selected face after a verb repairs/reorders the mesh. Face *indices* are not
// stable across BrushOps (each runs validate/repair), so multi-face operations
// resolve targets up front by identity and re-locate them in the working mesh.
struct FaceKey
{
    Vec3d Centroid;
    Vec3d Normal;
};

FaceKey MakeFaceKey(const BrushMesh& mesh, std::uint32_t face)
{
    return FaceKey{
        .Centroid = BrushFaceCentroid(mesh, mesh.Faces[face]),
        .Normal = BrushComputeFaceNormal(mesh, mesh.Faces[face]),
    };
}

std::optional<std::uint32_t> FindFace(const BrushMesh& mesh, const FaceKey& key)
{
    constexpr double kPosEps = 1.0e-4;
    constexpr double kNormalDot = 0.999;
    for (std::uint32_t i = 0; i < mesh.Faces.size(); ++i)
    {
        const Vec3d centroid = BrushFaceCentroid(mesh, mesh.Faces[i]);
        const Vec3d normal = BrushComputeFaceNormal(mesh, mesh.Faces[i]);
        if ((centroid - key.Centroid).SqrMagnitude() <= kPosEps * kPosEps
            && normal.Dot(key.Normal) >= kNormalDot)
            return i;
    }
    return std::nullopt;
}
}

MeshElementKind MeshEditService::GetElementKind() const
{
    return ElementKind;
}

void MeshEditService::SetElementKind(MeshElementKind kind)
{
    ElementKind = kind;
}

MeshElementKind MeshEditService::CycleElementKind()
{
    switch (ElementKind)
    {
    case MeshElementKind::Object:
        ElementKind = MeshElementKind::Vertex;
        break;
    case MeshElementKind::Vertex:
        ElementKind = MeshElementKind::Edge;
        break;
    case MeshElementKind::Edge:
        ElementKind = MeshElementKind::Face;
        break;
    case MeshElementKind::Face:
    default:
        ElementKind = MeshElementKind::Object;
        break;
    }
    return ElementKind;
}

std::unique_ptr<ICommand> MeshEditService::ApplyVerb(IMeshEditTarget& target,
                                                     const SelectionSnapshot& selection,
                                                     MeshEditVerb verb,
                                                     const MeshEditParams& params) const
{
    // Element translation works on vertex/edge/face refs (per the active mode),
    // not just faces, so it takes its own path rather than the face filter below.
    if (verb == MeshEditVerb::TranslateElements)
    {
        std::vector<SelectableRef> elements;
        for (SelectableRef ref : selection.Items)
        {
            if (ref.IsMeshElement())
                elements.push_back(ref);
        }
        if (elements.empty())
            return nullptr;

        const EntityId entity = elements.front().Entity;
        const RegistryId registry = elements.front().Registry;
        for (SelectableRef ref : elements)
        {
            if (ref.Entity != entity || ref.Registry != registry)
                return nullptr;
        }

        const std::optional<MeshEditTargetMesh> resolved = target.Resolve(entity);
        if (!resolved.has_value() || resolved->Mesh == nullptr)
            return nullptr;

        BrushMesh before = *resolved->Mesh;
        std::optional<BrushMesh> after = TranslateElements(
            before, resolved->Transform, elements, ElementKind, params.TranslateDelta, true);
        if (!after.has_value())
            return nullptr;

        return target.MakeEditCommand(entity, std::move(before), std::move(*after));
    }

    // Edge split also works on edge refs, not faces, so it takes its own path.
    if (verb == MeshEditVerb::SplitEdge)
    {
        std::vector<SelectableRef> edges;
        for (SelectableRef ref : selection.Items)
        {
            if (ref.IsEdge())
                edges.push_back(ref);
        }
        if (edges.empty())
            return nullptr;

        const EntityId entity = edges.front().Entity;
        const RegistryId registry = edges.front().Registry;
        for (SelectableRef ref : edges)
        {
            if (ref.Entity != entity || ref.Registry != registry)
                return nullptr;
        }

        const std::optional<MeshEditTargetMesh> resolved = target.Resolve(entity);
        if (!resolved.has_value() || resolved->Mesh == nullptr)
            return nullptr;

        BrushMesh before = *resolved->Mesh;
        BrushMesh after = before;
        // Resolve edge endpoints from `before`; SplitEdge only appends, so these
        // vertex indices stay valid as splits compose. One repair at the end.
        int applied = 0;
        for (SelectableRef ref : edges)
        {
            const std::optional<EdgeElement> edge =
                MeshElements::TryGetEdge(before, resolved->Transform, ref.ElementId);
            if (!edge.has_value())
                continue;
            after = BrushOps::SplitEdge(after, edge->VertexA, edge->VertexB);
            ++applied;
        }
        if (applied == 0)
            return nullptr;

        BrushRepairResult repair = BrushValidateAndRepair(after);
        if (!repair.Ok)
            return nullptr;

        return target.MakeEditCommand(entity, std::move(before), std::move(after));
    }

    std::vector<SelectableRef> faces;
    faces.reserve(selection.Items.size());
    for (SelectableRef ref : selection.Items)
    {
        if (ref.IsFace())
            faces.push_back(ref);
    }

    if (faces.empty())
        return nullptr;

    const EntityId entity = faces.front().Entity;
    const RegistryId registry = faces.front().Registry;
    for (SelectableRef ref : faces)
    {
        if (ref.Entity != entity || ref.Registry != registry)
            return nullptr;
    }

    const std::optional<MeshEditTargetMesh> resolved = target.Resolve(entity);
    if (!resolved.has_value() || resolved->Mesh == nullptr)
        return nullptr;

    BrushMesh before = *resolved->Mesh;
    BrushMesh after = before;

    // Resolve target faces by stable identity before mutating, so applying the
    // verb to one face can't invalidate the others (indices shift across repair).
    std::vector<FaceKey> faceKeys;
    faceKeys.reserve(faces.size());
    for (SelectableRef ref : faces)
    {
        if (ref.ElementId >= before.Faces.size())
            return nullptr;
        faceKeys.push_back(MakeFaceKey(before, ref.ElementId));
    }

    switch (verb)
    {
    case MeshEditVerb::Extrude:
    {
        int applied = 0;
        for (const FaceKey& key : faceKeys)
        {
            if (const std::optional<std::uint32_t> index = FindFace(after, key))
            {
                after = BrushOps::ExtrudeFace(after, *index, params.Distance);
                ++applied;
            }
        }
        if (applied == 0)
            return nullptr;
        break;
    }
    case MeshEditVerb::Delete:
    {
        int applied = 0;
        for (const FaceKey& key : faceKeys)
        {
            if (const std::optional<std::uint32_t> index = FindFace(after, key))
            {
                after = BrushOps::DeleteFace(after, *index);
                ++applied;
            }
        }
        if (applied == 0)
            return nullptr;
        break;
    }
    case MeshEditVerb::Clip:
        after = BrushOps::Clip(after, params.ClipPlane, params.KeepPositiveSide);
        break;
    case MeshEditVerb::ResizeFace:
        if (faces.size() != 1 || faces.front().ElementId >= after.Faces.size())
            return nullptr;
        after = BrushOps::ResizeFace(after, faces.front().ElementId,
                                     params.PlanePosition, params.MinThickness);
        break;
    case MeshEditVerb::TranslateElements:
    case MeshEditVerb::SplitEdge:
        return nullptr; // handled by the early branches above
    }

    BrushRepairResult repair = BrushValidateAndRepair(after);
    if (!repair.Ok)
        return nullptr;

    return target.MakeEditCommand(entity, std::move(before), std::move(after));
}

namespace
{
// Collects the unique local vertex indices referenced by `elements` under the
// given mode. Shared vertices (e.g. across selected faces or edges) appear once.
std::vector<std::uint32_t> GatherVertexIndices(const BrushMesh& mesh,
                                               const Transform3f& transform,
                                               std::span<const SelectableRef> elements,
                                               MeshElementKind kind)
{
    std::vector<std::uint32_t> indices;
    const auto add = [&](std::uint32_t index)
    {
        if (index >= mesh.Vertices.size())
            return;
        if (std::find(indices.begin(), indices.end(), index) == indices.end())
            indices.push_back(index);
    };

    for (SelectableRef ref : elements)
    {
        switch (kind)
        {
        case MeshElementKind::Vertex:
            if (ref.IsVertex())
                add(ref.ElementId);
            break;
        case MeshElementKind::Edge:
            if (ref.IsEdge())
            {
                if (const std::optional<EdgeElement> edge =
                        MeshElements::TryGetEdge(mesh, transform, ref.ElementId))
                {
                    add(edge->VertexA);
                    add(edge->VertexB);
                }
            }
            break;
        case MeshElementKind::Face:
            if (ref.IsFace() && ref.ElementId < mesh.Faces.size())
            {
                for (std::uint32_t v : mesh.Faces[ref.ElementId].Loop)
                    add(v);
            }
            break;
        case MeshElementKind::Object:
        default:
            break;
        }
    }

    return indices;
}
}

std::optional<BrushMesh> MeshEditService::TranslateElements(const BrushMesh& base,
                                                            const Transform3f& transform,
                                                            std::span<const SelectableRef> elements,
                                                            MeshElementKind kind,
                                                            Vec3d worldDelta,
                                                            bool validate) const
{
    const std::vector<std::uint32_t> indices =
        GatherVertexIndices(base, transform, elements, kind);
    if (indices.empty())
        return std::nullopt;

    // World delta -> local delta: undo the transform's rotation and scale. (The
    // translation component cancels for a delta, so only the linear part matters.)
    const Vec3d unrotated = transform.Rotation.Conjugate().RotateVector(worldDelta);
    const Vec3d localDelta(
        transform.Scale.X != 0.0f ? unrotated.X / transform.Scale.X : 0.0f,
        transform.Scale.Y != 0.0f ? unrotated.Y / transform.Scale.Y : 0.0f,
        transform.Scale.Z != 0.0f ? unrotated.Z / transform.Scale.Z : 0.0f);

    BrushMesh after = base;
    for (std::uint32_t index : indices)
        after.Vertices[index].Position += localDelta;

    if (validate)
    {
        BrushRepairResult repair = BrushValidateAndRepair(after);
        if (!repair.Ok)
            return std::nullopt;
    }

    return after;
}

namespace
{
double AxisGet(const Vec3d& v, int a) { return a == 0 ? v.X : (a == 1 ? v.Y : v.Z); }
void AxisSet(Vec3d& v, int a, double x)
{
    (a == 0 ? v.X : (a == 1 ? v.Y : v.Z)) = static_cast<float>(x);
}

// world -> local: undo the transform's translation, rotation, then scale.
Vec3d InverseTransformPoint(const Transform3f& t, Vec3d world)
{
    const Vec3d rel = world - t.Position;
    const Vec3d unrotated = t.Rotation.Conjugate().RotateVector(rel);
    return Vec3d(
        t.Scale.X != 0.0f ? unrotated.X / t.Scale.X : unrotated.X,
        t.Scale.Y != 0.0f ? unrotated.Y / t.Scale.Y : unrotated.Y,
        t.Scale.Z != 0.0f ? unrotated.Z / t.Scale.Z : unrotated.Z);
}
}

std::optional<BrushMesh> MeshEditService::ResizeBounds(const BrushMesh& base,
                                                       const Transform3f& transform,
                                                       Vec3d oldMin, Vec3d oldMax,
                                                       Vec3d newMin, Vec3d newMax,
                                                       bool validate) const
{
    constexpr double kEps = 1.0e-9;
    BrushMesh after = base;
    for (BrushVertex& vertex : after.Vertices)
    {
        Vec3d world = transform.TransformPoint(vertex.Position);
        for (int axis = 0; axis < 3; ++axis)
        {
            const double oldExtent = AxisGet(oldMax, axis) - AxisGet(oldMin, axis);
            const double newExtent = AxisGet(newMax, axis) - AxisGet(newMin, axis);
            // Degenerate source axis: leave the coordinate where it is.
            const double t = oldExtent > kEps
                ? (AxisGet(world, axis) - AxisGet(oldMin, axis)) / oldExtent
                : 0.0;
            AxisSet(world, axis, AxisGet(newMin, axis) + t * newExtent);
        }
        vertex.Position = InverseTransformPoint(transform, world);
    }

    if (validate)
    {
        BrushRepairResult repair = BrushValidateAndRepair(after);
        if (!repair.Ok)
            return std::nullopt;
    }

    return after;
}
