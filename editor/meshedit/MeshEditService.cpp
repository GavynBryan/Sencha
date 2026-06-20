#include "MeshEditService.h"

#include "ElementGeometry.h"
#include "MeshElementKindTraits.h"
#include "MeshElements.h"
#include "../level/brush/BrushOps.h"
#include "../level/brush/BrushValidation.h"

#include <algorithm>
#include <array>
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

// Collects the unique local vertex indices referenced by `elements` that match
// the active mode's selectable kind (so e.g. a stray face ref contributes nothing
// in vertex mode). Each matching ref reports its own vertices via the shared
// primitive; shared vertices (across faces/edges) appear once. No mode switch.
std::vector<std::uint32_t> GatherVertexIndices(const BrushMesh& mesh,
                                               const Transform3f& transform,
                                               std::span<const SelectableRef> elements,
                                               SelectableKind want)
{
    std::vector<std::uint32_t> indices;
    for (const SelectableRef& ref : elements)
    {
        if (ref.Kind != want)
            continue;
        for (std::uint32_t index : ElementVertexIndices(mesh, transform, ref))
            if (std::find(indices.begin(), indices.end(), index) == indices.end())
                indices.push_back(index);
    }
    return indices;
}

// Move the vertices the (mode-matching) refs reference by a world delta. Shared by
// the public TranslateElements member and the TranslateElements verb.
std::optional<BrushMesh> TranslateElementsImpl(const BrushMesh& base,
                                               const Transform3f& transform,
                                               std::span<const SelectableRef> elements,
                                               SelectableKind want, Vec3d worldDelta, bool validate)
{
    const std::vector<std::uint32_t> indices = GatherVertexIndices(base, transform, elements, want);
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

    if (validate && !BrushValidateAndRepair(after).Ok)
        return std::nullopt;
    return after;
}

// Everything a verb's apply needs: the resolved mesh + transform, the refs already
// filtered to the verb's element kind, the params, and the active element mode.
struct VerbContext
{
    const BrushMesh&               Before;
    const Transform3f&             Transform;
    std::span<const SelectableRef> Refs;
    const MeshEditParams&          Params;
    MeshElementKind                ElementKind;
};

// Per-face verbs (extrude/delete): resolve faces by stable identity up front so one
// face's edit can't invalidate another's index, apply per surviving face, validate.
template <typename Op>
std::optional<BrushMesh> ApplyPerFace(const BrushMesh& before, std::span<const SelectableRef> faces, Op op)
{
    std::vector<FaceKey> faceKeys;
    faceKeys.reserve(faces.size());
    for (const SelectableRef& ref : faces)
    {
        if (ref.ElementId >= before.Faces.size())
            return std::nullopt;
        faceKeys.push_back(MakeFaceKey(before, ref.ElementId));
    }

    BrushMesh after = before;
    int applied = 0;
    for (const FaceKey& key : faceKeys)
        if (const std::optional<std::uint32_t> index = FindFace(after, key))
        {
            after = op(after, *index);
            ++applied;
        }
    if (applied == 0)
        return std::nullopt;
    if (!BrushValidateAndRepair(after).Ok)
        return std::nullopt;
    return after;
}

std::optional<BrushMesh> ApplyExtrude(const VerbContext& ctx)
{
    return ApplyPerFace(ctx.Before, ctx.Refs, [d = ctx.Params.Distance](const BrushMesh& m, std::uint32_t i)
                        { return BrushOps::ExtrudeFace(m, i, d); });
}

std::optional<BrushMesh> ApplyDelete(const VerbContext& ctx)
{
    return ApplyPerFace(ctx.Before, ctx.Refs,
                        [](const BrushMesh& m, std::uint32_t i) { return BrushOps::DeleteFace(m, i); });
}

std::optional<BrushMesh> ApplyClip(const VerbContext& ctx)
{
    BrushMesh after = BrushOps::Clip(ctx.Before, ctx.Params.ClipPlane, ctx.Params.KeepPositiveSide);
    if (!BrushValidateAndRepair(after).Ok)
        return std::nullopt;
    return after;
}

std::optional<BrushMesh> ApplyResizeFace(const VerbContext& ctx)
{
    if (ctx.Refs.size() != 1 || ctx.Refs.front().ElementId >= ctx.Before.Faces.size())
        return std::nullopt;
    BrushMesh after = BrushOps::ResizeFace(ctx.Before, ctx.Refs.front().ElementId,
                                           ctx.Params.PlanePosition, ctx.Params.MinThickness);
    if (!BrushValidateAndRepair(after).Ok)
        return std::nullopt;
    return after;
}

std::optional<BrushMesh> ApplyTranslate(const VerbContext& ctx)
{
    return TranslateElementsImpl(ctx.Before, ctx.Transform, ctx.Refs,
                                 Traits(ctx.ElementKind).Selectable, ctx.Params.TranslateDelta, true);
}

std::optional<BrushMesh> ApplySplitEdge(const VerbContext& ctx)
{
    BrushMesh after = ctx.Before;
    int applied = 0;
    for (const SelectableRef& ref : ctx.Refs)
    {
        // Endpoints from Before; SplitEdge only appends, so indices stay valid as
        // splits compose. One repair at the end.
        if (const std::optional<EdgeElement> edge =
                MeshElements::TryGetEdge(ctx.Before, ctx.Transform, ref.ElementId))
        {
            after = BrushOps::SplitEdge(after, edge->VertexA, edge->VertexB);
            ++applied;
        }
    }
    if (applied == 0)
        return std::nullopt;
    if (!BrushValidateAndRepair(after).Ok)
        return std::nullopt;
    return after;
}

// Which refs a verb operates on, and how it transforms the mesh. Adding a verb =
// add the enum value + one row here (+ its apply above). Row order == MeshEditVerb.
struct VerbDescriptor
{
    bool (*Filter)(const SelectableRef&);
    std::optional<BrushMesh> (*Apply)(const VerbContext&);
};

bool IsFaceRef(const SelectableRef& r) { return r.IsFace(); }
bool IsEdgeRef(const SelectableRef& r) { return r.IsEdge(); }
bool IsMeshElementRef(const SelectableRef& r) { return r.IsMeshElement(); }

const std::array<VerbDescriptor, 6> kVerbs = { {
    { IsFaceRef,        ApplyExtrude },    // Extrude
    { IsFaceRef,        ApplyDelete },     // Delete
    { IsFaceRef,        ApplyClip },       // Clip
    { IsFaceRef,        ApplyResizeFace }, // ResizeFace
    { IsMeshElementRef, ApplyTranslate },  // TranslateElements
    { IsEdgeRef,        ApplySplitEdge },  // SplitEdge
} };
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
    ElementKind = Traits(ElementKind).Next;
    return ElementKind;
}

std::unique_ptr<ICommand> MeshEditService::ApplyVerb(IMeshEditTarget& target,
                                                     const SelectionSnapshot& selection,
                                                     MeshEditVerb verb,
                                                     const MeshEditParams& params) const
{
    const VerbDescriptor& desc = kVerbs[static_cast<std::size_t>(verb)];

    // Gather the refs this verb operates on; they must all belong to one entity.
    std::vector<SelectableRef> refs;
    for (const SelectableRef& ref : selection.Items)
        if (desc.Filter(ref))
            refs.push_back(ref);
    if (refs.empty())
        return nullptr;

    const EntityId entity = refs.front().Entity;
    const RegistryId registry = refs.front().Registry;
    for (const SelectableRef& ref : refs)
        if (ref.Entity != entity || ref.Registry != registry)
            return nullptr;

    const std::optional<MeshEditTargetMesh> resolved = target.Resolve(entity);
    if (!resolved.has_value() || resolved->Mesh == nullptr)
        return nullptr;

    BrushMesh before = *resolved->Mesh;
    const VerbContext ctx{ before, resolved->Transform, refs, params, ElementKind };
    std::optional<BrushMesh> after = desc.Apply(ctx);
    if (!after.has_value())
        return nullptr;

    return target.MakeEditCommand(entity, std::move(before), std::move(*after));
}

std::optional<BrushMesh> MeshEditService::TranslateElements(const BrushMesh& base,
                                                            const Transform3f& transform,
                                                            std::span<const SelectableRef> elements,
                                                            MeshElementKind kind,
                                                            Vec3d worldDelta,
                                                            bool validate) const
{
    return TranslateElementsImpl(base, transform, elements, Traits(kind).Selectable, worldDelta, validate);
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
