#pragma once

#include "brush/BrushEvaluation.h"

#include <ecs/EntityId.h>

#include <cstdint>
#include <unordered_map>

class EditorScene;

// What a volume brush evaluates to: its modifier evaluation with every mesh a
// volume. Cached per entity, since the role is the entity's and a record may be
// shared.
class VolumeEvaluations
{
public:
    // Valid until the entity's record or the policy changes, or it is forgotten.
    [[nodiscard]] const BrushEvaluated* Evaluate(const EditorScene& scene, EntityId entity,
                                                 const BrushEvaluationPolicy& policy);

    void Forget(EntityId entity);
    void Clear() { Entries.clear(); }

private:
    struct Entry
    {
        std::uint64_t  Inputs = 0;
        BrushEvaluated Value;
    };
    std::unordered_map<std::uint64_t, Entry> Entries;
};
