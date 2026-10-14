#pragma once

#include "brush/BrushEvaluation.h"
#include "brush/ShellGeneration.h"

#include <core/identity/Id.h>
#include <ecs/EntityId.h>

#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

class EditorScene;

// What a volume brush evaluates to: its modifier evaluation with every mesh a
// volume, and, when its entity has a shell, the slabs minted beside them as
// solid meshes whose inner face maps to the volume face it stands on. Cached
// per entity by everything it was built from (VolumeEvaluationInputs).
class VolumeEvaluations
{
public:
    // Valid until the entity's inputs change or it is forgotten.
    [[nodiscard]] const BrushEvaluated* Evaluate(const EditorScene& scene, EntityId entity,
                                                 const BrushEvaluationPolicy& policy);
    // What the last shell built for `entity` reported; empty without a shell.
    [[nodiscard]] std::span<const ShellProblem> ShellProblems(EntityId entity) const;
    // The openings that shell was cut by, in the order its OpeningRefused
    // problems number them.
    [[nodiscard]] std::span<const PersistentEntityId> ShellOpenings(EntityId entity) const;
    // The spaces that shell shares a wall with.
    [[nodiscard]] std::span<const PersistentEntityId> ShellSharedWith(EntityId entity) const;

    void Forget(EntityId entity);
    void Clear() { Entries.clear(); }

private:
    struct Entry
    {
        std::uint64_t             Inputs = 0;
        BrushEvaluated            Value;
        std::vector<ShellProblem> Problems;
        std::vector<PersistentEntityId> Openings;
        std::vector<PersistentEntityId> SharedWith;
    };
    std::unordered_map<std::uint64_t, Entry> Entries;
};

// A content hash of everything a volume's evaluation depends on: its record and
// the policy, and with a shell its thickness and world transform, the shelled
// volumes and openings near it, and the scene's open-face material. Zero for an
// entity that is not a volume brush.
[[nodiscard]] std::uint64_t VolumeEvaluationInputs(const EditorScene& scene, EntityId entity,
                                                   const BrushEvaluationPolicy& policy);
