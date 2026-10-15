#pragma once

#include "document/EntitySnapshot.h"

#include <core/identity/Id.h>
#include <ecs/EntityId.h>
#include <math/geometry/3d/Transform3d.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

class EditorDocument;
class EditorScene;

// An entity in the hierarchy means its branch: deleting, copying or moving it
// takes its descendants along.

// Every entity in the branches of `roots`, parents before children, each once.
// A root inside another root's branch rides along with it.
[[nodiscard]] std::vector<EntityId> CollectBranches(const EditorScene& scene, std::span<const EntityId> roots);

// What a source entity brings with it when copied. A source normally means
// its branch; a split, which turns one brush into two, copies the brush alone.
enum class DuplicateBranchPolicy : std::uint8_t
{
    Subtree,    // the source and every descendant, parents before children
    EntityOnly, // the source itself; its children stay where they are
};

// Sources captured as records, once, so every restore makes the same copies
// under fresh entities. Copies claim their own names at capture.
struct BranchCopy
{
    std::vector<EntitySnapshot> Snapshots; // parents before children
    // Each record's source identity: what a copied child's parent link names.
    std::vector<PersistentEntityId> SourceIds;
    // The index of the passed source each record is, or SIZE_MAX for a descendant.
    std::vector<std::size_t> RootOf;
};

using SnapshotRemap = std::function<void(std::vector<EntitySnapshot>&)>;

[[nodiscard]] BranchCopy CaptureBranchCopy(const EditorDocument& document, std::span<const EntityId> sources,
                                           DuplicateBranchPolicy branch, const SnapshotRemap& remap);

// The copies, in record order, each one's parent link rebound to the copy of
// its parent. A passed source's copy goes to its placement, when one is given,
// in world space; descendants keep their local transforms. With `asInstance`
// the copies share their sources' brush meshes.
std::vector<EntityId> RestoreBranchCopy(EditorDocument& document, const BranchCopy& copy,
                                        std::span<const Transform3f> placements, bool asInstance);
