#pragma once

#include "BrushBounds.h"
#include "BrushMesh.h"
#include "BrushModifier.h"
#include "BrushWorkCounters.h"

#include <math/geometry/3d/Aabb3d.h>
#include <math/geometry/3d/Plane.h>
#include <math/geometry/3d/Transform3d.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

//=============================================================================
// Brush modifier evaluation: piece set -> modifier -> piece set.
//
// A modifier may change how many pieces a brush produces (Mirror doubles, Array
// multiplies), so the unit of evaluation is a set of pieces, each a mesh
// reference at a brush-local placement, rather than a single mesh. Array emits
// placements that share one mesh; Mirror mints one reflected mesh per distinct
// input mesh. Reflections are always baked into meshes: a placement is never
// an improper transform, so tessellation and rendering keep their
// positive-determinant assumption.
//=============================================================================

// "No modifier": the source mesh's MintedBy and the source piece's ProducedBy.
inline constexpr std::uint32_t kBrushNoModifier = 0xFFFFFFFFu;
inline constexpr std::uint32_t kBrushNoElement = 0xFFFFFFFFu;

// Evaluated element -> source element for one distinct mesh, per element
// kind. Identity: the same index. Table: one entry per evaluated element,
// the source index or kBrushNoElement; several evaluated elements may name
// one source element, so a modifier that splits a face is representable, and
// the one-to-many inverse is derived by the consumer that needs it. None: the
// modifier destroyed the correspondence. Edges follow vertices: an evaluated
// edge maps to the source edge between its vertices' source images.
enum class BrushElementMapKind : std::uint8_t { Identity, Table, None };
struct BrushElementMap
{
    BrushElementMapKind        Faces = BrushElementMapKind::Identity;
    BrushElementMapKind        Vertices = BrushElementMapKind::Identity;
    std::vector<std::uint32_t> FaceTable;
    std::vector<std::uint32_t> VertexTable;
};

// One distinct mesh an evaluation draws from: the source (always entry 0) or
// a mesh a modifier minted. Everything that outlives the evaluation keys on
// Signature, never on the pointer: the next evaluation mints new meshes.
// Mesh lineage lives here (MintedBy / MintedFrom); placement lineage lives on
// the piece (ProducedBy). They answer different questions: an Array copy of a
// mirrored wall was produced by the Array and draws a mesh minted by the Mirror.
struct BrushEvaluatedMesh
{
    const BrushMesh* Mesh = nullptr;
    Aabb3d           LocalBounds = Aabb3d::Empty(); // in the mesh's own frame, before placement
    std::uint64_t    Signature = 0;                 // BrushMeshSignature(*Mesh)
    std::uint32_t    MintedBy = kBrushNoModifier;   // stack index of the modifier that minted it
    std::uint32_t    MintedFrom = 0;                // MeshIndex it derived from (itself for the source)
    BrushElementMap  ToSource;                      // composed through the whole lineage
    // Edge topology in BrushEdgePairs order: the enumeration every edge index
    // refers to, computed once here so no consumer re-enumerates.
    std::vector<std::array<std::uint32_t, 2>> EdgePairs;
    std::vector<bool>                         EdgeSoft;
};

enum class BrushPieceOrigin : std::uint8_t
{
    Source,    // the authored instance at its own placement; exactly one per evaluation
    Generated, // a copy a modifier emitted
};

struct BrushPiece
{
    // The source mesh (owned by the caller) or one owned by BrushEvaluated::Generated;
    // always BrushEvaluated::Meshes[MeshIndex].Mesh.
    const BrushMesh* Mesh = nullptr;
    std::uint32_t    MeshIndex = 0;
    // Brush-local; the entity's world transform composes on top (PieceWorldTransform).
    // General on purpose. The current modifiers emit translations, and callers
    // that can go faster for a translation check for one rather than assume it.
    Transform3f      Placement = Transform3f::Identity();
    std::uint32_t    Ordinal = 0; // index in BrushEvaluated::Pieces
    BrushPieceOrigin Origin = BrushPieceOrigin::Source;
    // The stack index of the modifier that emitted this piece as an output
    // distinct from its input; kBrushNoModifier for the source piece. One hop:
    // an Array's copies say Array whatever mesh they draw.
    std::uint32_t    ProducedBy = kBrushNoModifier;
};

enum class BrushEvaluationStatus : std::uint8_t
{
    Ok,
    PieceBudgetExceeded,
};

// The third input of evaluation, (source, stack, policy) -> result. Cook() is
// the hard limit every machine agrees on, so whether a document cooks never
// depends on who cooks it; Interactive(n) may only be lower and governs the
// editor preview alone.
struct BrushEvaluationPolicy
{
    static constexpr std::uint32_t kHardPieceLimit = 65536;

    std::uint32_t MaxPieces = kHardPieceLimit;

    [[nodiscard]] static BrushEvaluationPolicy Cook() { return {}; }
    [[nodiscard]] static BrushEvaluationPolicy Interactive(std::uint32_t previewBudget)
    {
        return BrushEvaluationPolicy{ previewBudget < kHardPieceLimit ? previewBudget
                                                                       : kHardPieceLimit };
    }

    bool operator==(const BrushEvaluationPolicy&) const = default;
};

// What one stack entry resolved its relationship to: the piece set it consumed
// and the plane or step it derived from it. Derived data for the inspector and
// the viewport, so nothing outside the evaluator re-derives a slightly
// different answer; never serialized.
struct BrushStageResolution
{
    Aabb3d InputBounds = Aabb3d::Empty(); // local AABB of the piece set the stage consumed
    Plane  MirrorPlane;                   // Mirror: the plane it reflected across
    Vec3d  ArrayStep;                     // Array: the translation between copies
    bool   Applied = false;               // false when disabled or stopped by the budget
};

struct BrushEvaluated
{
    std::vector<std::unique_ptr<BrushMesh>> Generated; // minted by modifiers; pieces alias these
    std::vector<BrushEvaluatedMesh>         Meshes;    // source first, then each of Generated in order
    std::vector<BrushPiece>                 Pieces;
    std::uint32_t                           SourcePiece = 0; // ordinal of the Origin == Source piece
    Aabb3d                                  LocalBounds = Aabb3d::Empty(); // union of placed pieces, brush-local
    std::vector<BrushStageResolution>       Stages;    // one per stack entry, in stack order
    BrushEvaluationStatus                   Status = BrushEvaluationStatus::Ok;
    // The stack index evaluation stopped at when Status != Ok. Pieces then hold
    // the result of the modifiers ahead of it.
    std::uint32_t                           FailedModifier = 0;
};

// Pure and deterministic. Disabled modifiers are skipped. An empty stack yields
// one piece aliasing `source` and mints nothing. Before a modifier allocates,
// its output count is checked against the policy; a modifier that would exceed
// it is not run and the status names it, so a mistyped count can never hang
// the editor or cook a partial brush unnoticed. The result aliases `source`,
// which must outlive it.
[[nodiscard]] BrushEvaluated EvaluateBrushModifiers(const BrushMesh& source,
                                                    const BrushModifierStack& stack,
                                                    const BrushEvaluationPolicy& policy);

// The source element an evaluated element stands for, or nullopt for one a
// modifier invented, answered through the piece's mesh ToSource map. Pickers
// and highlighting ask these instead of assuming generated elements number
// like source elements. Edges take the evaluated mesh's edge index (EdgePairs
// order) and answer with the source mesh's.
[[nodiscard]] std::optional<std::uint32_t> SourceFaceFor(const BrushEvaluated& evaluated,
                                                          const BrushPiece& piece,
                                                          std::uint32_t evaluatedFace);
[[nodiscard]] std::optional<std::uint32_t> SourceVertexFor(const BrushEvaluated& evaluated,
                                                            const BrushPiece& piece,
                                                            std::uint32_t evaluatedVertex);
[[nodiscard]] std::optional<std::uint32_t> SourceEdgeFor(const BrushEvaluated& evaluated,
                                                          const BrushPiece& piece,
                                                          std::uint32_t evaluatedEdge);
// The index of the source edge (a, b) in the source mesh's EdgePairs, if any.
[[nodiscard]] std::optional<std::uint32_t> SourceEdgeIndexOf(const BrushEvaluated& evaluated,
                                                              std::uint32_t a, std::uint32_t b);

// Local AABB of a piece set: every piece's mesh bounds at its placement. The
// bounds a stage's relationship is resolved against. `meshes` supplies each
// piece's mesh bounds so no piece is re-measured.
[[nodiscard]] Aabb3d PieceSetLocalBounds(std::span<const BrushEvaluatedMesh> meshes,
                                         std::span<const BrushPiece> pieces);

// How many pieces the stack would produce, before evaluating anything: the
// product of the enabled modifiers' output factors, saturating. What a budget
// check or an inspector warning needs without minting a mesh.
[[nodiscard]] std::uint64_t BrushProjectedPieceCount(const BrushModifierStack& stack);

// Whether a placement moves points without turning or scaling them.
[[nodiscard]] bool IsPureTranslation(const Transform3f& placement);

// Where a piece sits in the world: the one place the entity transform and a
// placement compose.
[[nodiscard]] Transform3f PieceWorldTransform(const Transform3f& entityWorld,
                                              const BrushPiece& piece);

// Tight world AABB per piece: fn(const BrushPiece&, const Aabb3d& world). The
// box is vertex-tight under the piece's full world transform (entity *
// placement). A piece whose placement is a pure translation reuses its mesh's
// box rotated once at the entity transform and shifts it, so a stack of
// translations costs O(V * distinct meshes + pieces); any other placement pays
// O(V) for that piece.
template <class F>
void ForEachPieceWorldBounds(const BrushEvaluated& evaluated, const Transform3f& world, F&& fn)
{
    ++BrushWorkCounters::Frame().PieceWalks;
    std::vector<Aabb3d> rotated(evaluated.Meshes.size(), Aabb3d::Empty());
    std::vector<bool> measured(evaluated.Meshes.size(), false);
    for (const BrushPiece& piece : evaluated.Pieces)
    {
        if (piece.Mesh == nullptr || piece.Mesh->Vertices.empty())
            continue;
        if (!IsPureTranslation(piece.Placement) || piece.MeshIndex >= rotated.size())
        {
            fn(piece, BrushWorldBounds(*piece.Mesh, PieceWorldTransform(world, piece)));
            continue;
        }
        if (!measured[piece.MeshIndex])
        {
            rotated[piece.MeshIndex] = BrushWorldBounds(*piece.Mesh, world);
            measured[piece.MeshIndex] = true;
        }
        const Vec3d shift = world.TransformVector(piece.Placement.Position);
        const Aabb3d& base = rotated[piece.MeshIndex];
        fn(piece, Aabb3d::FromMinMax(base.Min + shift, base.Max + shift));
    }
}

// Every piece appended into one mesh in the brush's own frame (placements
// applied, texture placement preserved), welded and repaired: the destructive
// form of the evaluated result, for baking and merging.
[[nodiscard]] BrushMesh FlattenBrushPieces(const BrushEvaluated& evaluated);

