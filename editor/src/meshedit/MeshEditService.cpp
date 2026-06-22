#include "MeshEditService.h"

#include "ElementGeometry.h"
#include "MeshElementKindTraits.h"
#include "MeshElements.h"
#include "../level/brush/BrushHalfEdge.h"
#include "../level/brush/BrushOps.h"
#include "../level/brush/BrushValidation.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <unordered_map>
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

std::optional<BrushMesh> ApplyFlipNormal(const VerbContext& ctx)
{
    return ApplyPerFace(ctx.Before, ctx.Refs,
                        [](const BrushMesh& m, std::uint32_t i) { return BrushOps::FlipFace(m, i); });
}

std::optional<BrushMesh> ApplyRecalculateNormals(const VerbContext& ctx)
{
    BrushMesh after = ctx.Before;
    BrushOrientFacesOutward(after);
    if (!BrushValidateAndRepair(after).Ok)
        return std::nullopt;
    return after;
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
bool IsEntityRef(const SelectableRef& r) { return r.IsEntity(); }

const std::array<VerbDescriptor, 8> kVerbs = { {
    { IsFaceRef,        ApplyExtrude },             // Extrude
    { IsFaceRef,        ApplyDelete },              // Delete
    { IsFaceRef,        ApplyClip },                // Clip
    { IsFaceRef,        ApplyResizeFace },          // ResizeFace
    { IsMeshElementRef, ApplyTranslate },           // TranslateElements
    { IsEdgeRef,        ApplySplitEdge },           // SplitEdge
    { IsFaceRef,        ApplyFlipNormal },          // FlipFaceNormal
    { IsEntityRef,      ApplyRecalculateNormals },  // RecalculateNormals
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
// World delta -> local delta: undo the transform's rotation and scale only (the
// translation cancels for a delta). Shared by the extrude path. Mirrors the
// inline conversion in TranslateElementsImpl.
Vec3d WorldToLocalDelta(const Transform3f& transform, Vec3d worldDelta)
{
    const Vec3d unrotated = transform.Rotation.Conjugate().RotateVector(worldDelta);
    return Vec3d(
        transform.Scale.X != 0.0f ? unrotated.X / transform.Scale.X : 0.0f,
        transform.Scale.Y != 0.0f ? unrotated.Y / transform.Scale.Y : 0.0f,
        transform.Scale.Z != 0.0f ? unrotated.Z / transform.Scale.Z : 0.0f);
}

// Undirected endpoint match for re-finding a new edge after repair reindexes.
bool EdgeEndpointsMatch(Vec3d a0, Vec3d b0, Vec3d a1, Vec3d b1)
{
    constexpr float kEps = 1.0e-4f;
    const auto near = [](Vec3d p, Vec3d q) { return (p - q).SqrMagnitude() <= kEps * kEps; };
    return (near(a0, a1) && near(b0, b1)) || (near(a0, b1) && near(b0, a1));
}

std::uint64_t UndirectedEdgeKey(std::uint32_t a, std::uint32_t b)
{
    return (static_cast<std::uint64_t>(std::min(a, b)) << 32) | std::max(a, b);
}

// Directed endpoints (origin -> target) of each undirected edge as its bordering
// face winds it. A pulled flap is then wound the OPPOSITE way across the shared
// edge so its orientation continues the source face's (the manifold-consistency
// rule): this is the single rule that makes an in-plane extrude keep facing like
// the floor and a vertical extrude fold that facing up into the wall. The raw
// EdgeElement order is normalized to (min,max) and on its own is a per-edge coin
// flip. Boundary edges have one bordering half-edge; an interior edge keeps the
// first visited.
std::unordered_map<std::uint64_t, std::pair<std::uint32_t, std::uint32_t>>
EdgeSourceWinding(const BrushMesh& mesh)
{
    const BrushHalfEdgeMesh he = BrushBuildHalfEdge(mesh);
    std::unordered_map<std::uint64_t, std::pair<std::uint32_t, std::uint32_t>> directed;
    for (const BrushHalfEdge& edge : he.HalfEdges)
    {
        if (edge.Origin >= mesh.Vertices.size() || edge.Next >= he.HalfEdges.size())
            continue;
        const std::uint32_t target = he.HalfEdges[edge.Next].Origin;
        if (target >= mesh.Vertices.size() || target == edge.Origin)
            continue;
        directed.emplace(UndirectedEdgeKey(edge.Origin, target),
                         std::pair{ edge.Origin, target });
    }
    return directed;
}
}

std::optional<MeshEditService::ExtrudeResult> MeshEditService::ExtrudeElements(
    const BrushMesh& base,
    const Transform3f& transform,
    std::span<const SelectableRef> elements,
    MeshElementKind kind,
    Vec3d worldDelta,
    bool /*validate*/) const
{
    const Vec3d offset = WorldToLocalDelta(transform, worldDelta);
    // Below the weld tolerance the extruded ring merges back into the base (repair
    // welds it), so there is nothing to extrude: report no-op so the drag previews
    // the original and commits nothing.
    constexpr float kMinExtrude = 1e-4f;
    if (offset.SqrMagnitude() <= kMinExtrude * kMinExtrude)
        return std::nullopt;
    const SelectableKind want = Traits(kind).Selectable;

    if (want == SelectableKind::Face)
    {
        // Resolve sources and their post-extrude caps up front by identity: each
        // extrude repairs/reindexes, so neither the source nor the cap can be
        // addressed by a fixed index across the loop. The cap is the source loop
        // translated rigidly by offset (centroid + offset, same normal).
        std::vector<FaceKey> sourceKeys;
        std::vector<FaceKey> capKeys;
        for (const SelectableRef& ref : elements)
        {
            if (!ref.IsFace())
                continue;
            if (ref.ElementId >= base.Faces.size())
                return std::nullopt;
            const Vec3d centroid = BrushFaceCentroid(base, base.Faces[ref.ElementId]);
            const Vec3d normal = BrushComputeFaceNormal(base, base.Faces[ref.ElementId]);
            sourceKeys.push_back(FaceKey{ centroid, normal });
            capKeys.push_back(FaceKey{ centroid + offset, normal });
        }
        if (sourceKeys.empty())
            return std::nullopt;

        BrushMesh after = base;
        int applied = 0;
        for (const FaceKey& key : sourceKeys)
            if (const std::optional<std::uint32_t> index = FindFace(after, key))
            {
                after = BrushOps::ExtrudeFaceAlong(after, *index, offset);
                ++applied;
            }
        if (applied == 0 || !BrushValidateAndRepair(after).Ok)
            return std::nullopt;

        std::vector<std::uint32_t> ids;
        for (const FaceKey& key : capKeys)
            if (const std::optional<std::uint32_t> index = FindFace(after, key))
                ids.push_back(*index);
        return ExtrudeResult{ std::move(after), std::move(ids) };
    }

    if (want == SelectableKind::Edge)
    {
        // Wind each flap to continue the source face's orientation across the shared
        // edge (opposite traversal), so in-plane and vertical extrudes stay
        // consistent with the floor instead of coin-flipping per edge.
        const auto sourceWinding = EdgeSourceWinding(base);

        BrushMesh after = base;
        int applied = 0;
        std::vector<std::pair<Vec3d, Vec3d>> newEdges; // local endpoints of each pulled edge
        for (const SelectableRef& ref : elements)
        {
            if (!ref.IsEdge())
                continue;
            // Endpoints from base; ExtrudeEdge only appends, so indices stay valid
            // as strips compose. One repair at the end.
            if (const std::optional<EdgeElement> edge =
                    MeshElements::TryGetEdge(base, transform, ref.ElementId))
            {
                std::uint32_t va = edge->VertexA;
                std::uint32_t vb = edge->VertexB;
                // ExtrudeEdge winds the flap base va -> vb. The source face traverses
                // the edge origin -> target, so wind the flap target -> origin to
                // continue its orientation (manifold-consistent).
                if (const auto it = sourceWinding.find(UndirectedEdgeKey(va, vb));
                    it != sourceWinding.end())
                {
                    va = it->second.second; // source target
                    vb = it->second.first;  // source origin
                }
                const Vec3d a = base.Vertices[va].Position;
                const Vec3d b = base.Vertices[vb].Position;
                after = BrushOps::ExtrudeEdge(after, va, vb, offset);
                newEdges.emplace_back(a + offset, b + offset);
                ++applied;
            }
        }
        if (applied == 0 || !BrushValidateAndRepair(after).Ok)
            return std::nullopt;

        std::vector<std::uint32_t> ids;
        const std::vector<EdgeElement> edges = MeshElements::Edges(after, Transform3f::Identity());
        for (const auto& [a, b] : newEdges)
            for (const EdgeElement& e : edges)
                if (EdgeEndpointsMatch(e.A, e.B, a, b))
                {
                    ids.push_back(e.Index);
                    break;
                }
        return ExtrudeResult{ std::move(after), std::move(ids) };
    }

    return std::nullopt;
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
