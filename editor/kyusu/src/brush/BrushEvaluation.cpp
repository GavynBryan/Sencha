#include "BrushEvaluation.h"

#include "BrushOps.h"
#include "BrushValidation.h"

#include <math/geometry/3d/AabbTransform.h>

#include <algorithm>
#include <limits>
#include <utility>

namespace
{
    // How many pieces a modifier emits per input piece.
    struct OutputFactor
    {
        std::uint64_t operator()(const MirrorModifier&) const { return 2; }
        std::uint64_t operator()(const ArrayModifier& array) const
        {
            return static_cast<std::uint64_t>(array.Count < 1 ? 1 : array.Count);
        }
    };

    std::uint32_t MeshIndexOf(const BrushEvaluated& evaluated, const BrushMesh* mesh)
    {
        for (std::uint32_t i = 0; i < evaluated.Meshes.size(); ++i)
            if (evaluated.Meshes[i].Mesh == mesh)
                return i;
        return 0;
    }

    BrushEvaluatedMesh MakeEvaluatedMesh(const BrushMesh& mesh)
    {
        BrushEvaluatedMesh out;
        out.Mesh = &mesh;
        out.LocalBounds = BrushComputeBounds(mesh);
        out.Signature = BrushMeshSignature(mesh);
        out.EdgePairs = BrushEdgePairs(mesh);
        out.EdgeSoft.reserve(out.EdgePairs.size());
        for (const auto& edge : out.EdgePairs)
            out.EdgeSoft.push_back(!mesh.SoftEdges.empty() && BrushEdgeIsSoft(mesh, edge[0], edge[1]));
        return out;
    }

    struct Apply
    {
        BrushEvaluated& Out;
        BrushStageResolution& Stage;
        std::uint32_t StageIndex;

        void operator()(const MirrorModifier& mirror) const
        {
            const Plane resolved = ResolveMirrorPlane(mirror, Stage.InputBounds);
            if (resolved.Normal.SqrMagnitude() < 1e-12f)
                return;
            const Plane plane = resolved.Normalized();
            Stage.MirrorPlane = plane;
            Stage.Applied = true;

            // The reflection of a placed piece is the reflected mesh at the
            // reflected translation, so pieces that share a mesh keep sharing
            // its reflection.
            const std::size_t inputCount = Out.Pieces.size();
            std::vector<std::pair<const BrushMesh*, const BrushMesh*>> reflected;
            Out.Pieces.reserve(inputCount * 2);
            for (std::size_t k = 0; k < inputCount; ++k)
            {
                const BrushPiece input = Out.Pieces[k];
                const BrushMesh* mesh = nullptr;
                for (const auto& [from, to] : reflected)
                    if (from == input.Mesh)
                        mesh = to;
                if (mesh == nullptr)
                {
                    Out.Generated.push_back(
                        std::make_unique<BrushMesh>(MirrorBrushMesh(*input.Mesh, plane)));
                    mesh = Out.Generated.back().get();
                    reflected.emplace_back(input.Mesh, mesh);
                    // A reflection keeps every index, so the map to the source
                    // is the input mesh's own.
                    BrushEvaluatedMesh minted = MakeEvaluatedMesh(*mesh);
                    minted.MintedBy = StageIndex;
                    minted.MintedFrom = input.MeshIndex;
                    minted.ToSource = Out.Meshes[input.MeshIndex].ToSource;
                    Out.Meshes.push_back(std::move(minted));
                }
                BrushPiece piece = input;
                piece.Mesh = mesh;
                piece.MeshIndex = MeshIndexOf(Out, mesh);
                piece.Placement.Position = ReflectVector(plane.Normal, input.Placement.Position);
                piece.Origin = BrushPieceOrigin::Generated;
                piece.ProducedBy = StageIndex;
                Out.Pieces.push_back(piece);
            }
        }

        void operator()(const ArrayModifier& array) const
        {
            const int count = array.Count < 1 ? 1 : array.Count;
            const Vec3d step = ResolveArrayStep(array, Stage.InputBounds);
            Stage.ArrayStep = step;
            Stage.Applied = true;
            std::vector<BrushPiece> pieces;
            pieces.reserve(Out.Pieces.size() * static_cast<std::size_t>(count));
            for (const BrushPiece& input : Out.Pieces)
            {
                for (int i = 0; i < count; ++i)
                {
                    BrushPiece piece = input;
                    piece.Placement.Position =
                        input.Placement.Position + step * static_cast<float>(i);
                    if (i > 0)
                    {
                        piece.Origin = BrushPieceOrigin::Generated;
                        piece.ProducedBy = StageIndex;
                    }
                    pieces.push_back(piece);
                }
            }
            Out.Pieces = std::move(pieces);
        }
    };
}

BrushEvaluated EvaluateBrushModifiers(const BrushMesh& source,
                                      const BrushModifierStack& stack,
                                      const BrushEvaluationPolicy& policy)
{
    ++BrushWorkCounters::Frame().Evaluations;
    BrushEvaluated out;
    out.Meshes.push_back(MakeEvaluatedMesh(source));
    out.Meshes[0].MintedFrom = 0;
    BrushPiece sourcePiece;
    sourcePiece.Mesh = &source;
    out.Pieces.push_back(sourcePiece);
    out.Stages.resize(stack.size());

    for (std::size_t i = 0; i < stack.size(); ++i)
    {
        const BrushModifier& modifier = stack[i];
        if (!modifier.Enabled)
            continue;
        const std::uint64_t factor = std::visit(OutputFactor{}, modifier.Params);
        const std::uint64_t projected = static_cast<std::uint64_t>(out.Pieces.size()) * factor;
        if (projected > policy.MaxPieces)
        {
            out.Status = BrushEvaluationStatus::PieceBudgetExceeded;
            out.FailedModifier = static_cast<std::uint32_t>(i);
            break;
        }
        out.Stages[i].InputBounds = PieceSetLocalBounds(out.Meshes, out.Pieces);
        std::visit(Apply{ out, out.Stages[i], static_cast<std::uint32_t>(i) }, modifier.Params);
    }

    for (std::size_t i = 0; i < out.Pieces.size(); ++i)
    {
        out.Pieces[i].Ordinal = static_cast<std::uint32_t>(i);
        if (out.Pieces[i].Origin == BrushPieceOrigin::Source)
            out.SourcePiece = static_cast<std::uint32_t>(i);
    }
    out.LocalBounds = PieceSetLocalBounds(out.Meshes, out.Pieces);
    return out;
}

namespace
{
    std::optional<std::uint32_t> MapElement(BrushElementMapKind kind,
                                            const std::vector<std::uint32_t>& table,
                                            std::uint32_t evaluated, std::size_t count)
    {
        if (evaluated >= count)
            return std::nullopt;
        switch (kind)
        {
        case BrushElementMapKind::Identity:
            return evaluated;
        case BrushElementMapKind::Table:
            if (evaluated >= table.size() || table[evaluated] == kBrushNoElement)
                return std::nullopt;
            return table[evaluated];
        case BrushElementMapKind::None:
        default:
            return std::nullopt;
        }
    }
}

std::optional<std::uint32_t> SourceFaceFor(const BrushEvaluated& evaluated, const BrushPiece& piece,
                                           std::uint32_t evaluatedFace)
{
    if (piece.Mesh == nullptr || piece.MeshIndex >= evaluated.Meshes.size())
        return std::nullopt;
    const BrushElementMap& map = evaluated.Meshes[piece.MeshIndex].ToSource;
    return MapElement(map.Faces, map.FaceTable, evaluatedFace, piece.Mesh->Faces.size());
}

std::optional<std::uint32_t> SourceVertexFor(const BrushEvaluated& evaluated, const BrushPiece& piece,
                                             std::uint32_t evaluatedVertex)
{
    if (piece.Mesh == nullptr || piece.MeshIndex >= evaluated.Meshes.size())
        return std::nullopt;
    const BrushElementMap& map = evaluated.Meshes[piece.MeshIndex].ToSource;
    return MapElement(map.Vertices, map.VertexTable, evaluatedVertex, piece.Mesh->Vertices.size());
}

std::optional<std::uint32_t> SourceEdgeIndexOf(const BrushEvaluated& evaluated,
                                               std::uint32_t a, std::uint32_t b)
{
    if (evaluated.Meshes.empty())
        return std::nullopt;
    const auto& pairs = evaluated.Meshes[0].EdgePairs;
    const auto key = BrushSoftEdgeKey(a, b);
    const auto it = std::lower_bound(pairs.begin(), pairs.end(), key);
    if (it == pairs.end() || *it != key)
        return std::nullopt;
    return static_cast<std::uint32_t>(it - pairs.begin());
}

std::optional<std::uint32_t> SourceEdgeFor(const BrushEvaluated& evaluated, const BrushPiece& piece,
                                           std::uint32_t evaluatedEdge)
{
    if (piece.MeshIndex >= evaluated.Meshes.size())
        return std::nullopt;
    const BrushEvaluatedMesh& mesh = evaluated.Meshes[piece.MeshIndex];
    if (evaluatedEdge >= mesh.EdgePairs.size())
        return std::nullopt;
    const auto& pair = mesh.EdgePairs[evaluatedEdge];
    const std::optional<std::uint32_t> a = SourceVertexFor(evaluated, piece, pair[0]);
    const std::optional<std::uint32_t> b = SourceVertexFor(evaluated, piece, pair[1]);
    if (!a.has_value() || !b.has_value())
        return std::nullopt;
    return SourceEdgeIndexOf(evaluated, *a, *b);
}

Aabb3d PieceSetLocalBounds(std::span<const BrushEvaluatedMesh> meshes,
                           std::span<const BrushPiece> pieces)
{
    Aabb3d bounds = Aabb3d::Empty();
    for (const BrushPiece& piece : pieces)
    {
        if (piece.MeshIndex >= meshes.size() || !meshes[piece.MeshIndex].LocalBounds.IsValid())
            continue;
        const Aabb3d& local = meshes[piece.MeshIndex].LocalBounds;
        if (IsPureTranslation(piece.Placement))
        {
            bounds.ExpandToInclude(local.Min + piece.Placement.Position);
            bounds.ExpandToInclude(local.Max + piece.Placement.Position);
        }
        else
            bounds.ExpandToInclude(TransformAabb(local, piece.Placement.ToMat4()));
    }
    return bounds;
}

std::uint64_t BrushProjectedPieceCount(const BrushModifierStack& stack)
{
    std::uint64_t count = 1;
    for (const BrushModifier& modifier : stack)
    {
        if (!modifier.Enabled)
            continue;
        const std::uint64_t factor = std::visit(OutputFactor{}, modifier.Params);
        if (factor != 0 && count > std::numeric_limits<std::uint64_t>::max() / factor)
            return std::numeric_limits<std::uint64_t>::max();
        count *= factor;
    }
    return count;
}

bool IsPureTranslation(const Transform3f& placement)
{
    return placement.Rotation.NearlyEquals(Quat<float>::Identity(), 0.0f)
        && placement.Scale.X == 1.0f && placement.Scale.Y == 1.0f && placement.Scale.Z == 1.0f;
}

BrushMesh FlattenBrushPieces(const BrushEvaluated& evaluated)
{
    BrushMesh out;
    for (const BrushPiece& piece : evaluated.Pieces)
        if (piece.Mesh != nullptr)
            BrushOps::AppendRebased(out, Transform3f::Identity(), *piece.Mesh, piece.Placement);
    BrushValidateAndRepair(out);
    return out;
}

Transform3f PieceWorldTransform(const Transform3f& entityWorld, const BrushPiece& piece)
{
    return entityWorld * piece.Placement;
}
