#pragma once

#include <render/static_mesh/MeshGeometry.h>

#include <cstdint>
#include <string>
#include <vector>

//=============================================================================
// SkinnedMeshData (docs/assets/pipeline.md, Decisions J, M, N)
//
// A skinned mesh is geometry plus the skinning that binds it to a skeleton.
// It is a distinct asset type from a static mesh, not a static mesh with an
// optional field: the runtime diverges hard (bone palette, pose evaluation,
// a skinning pass, per-instance posed buffers — Decision N), so the type
// system distinguishes them and every consumer dispatches without cracking
// open the payload. The geometry itself is the shared MeshGeometry core.
//=============================================================================

// Per-vertex skinning influences — the Decision M separate stream, never
// interleaved with the base vertex: the static geometry stays byte-identical,
// and both Decision N skinning candidates can consume it (vertex attributes
// or storage buffer) without re-cooking. Joint indices are skeleton-local
// (resolved at cook); weights are unorm8, normalized at cook to sum exactly
// 255 — the runtime never fixes data. Zero-weight slots carry joint 0 so
// cook-side welds stay deterministic.
struct MeshSkinInfluence
{
    uint16_t Joints[4]{};
    uint8_t Weights[4]{};
};

static_assert(sizeof(MeshSkinInfluence) == 12);

struct MeshSkinning
{
    // The skeleton this mesh is bound to ("asset://..."), resolved through
    // the front door at load and retained by the cache entry (the
    // mesh→skeleton refcount chain, the material→texture composition).
    std::string SkeletonPath;

    // Number of joints the influences index (== the skeleton's joint count
    // at cook). Recorded in the container header so either skinning runtime
    // can size palettes/buffers from the header alone (Decision N).
    uint32_t JointCount = 0;

    // One influence per vertex, parallel to Geometry.Vertices.
    std::vector<MeshSkinInfluence> Influences;
};

struct SkinnedMeshData
{
    MeshGeometry Geometry;
    MeshSkinning Skinning;
};
