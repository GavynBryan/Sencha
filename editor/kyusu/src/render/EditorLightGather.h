#pragma once

#include <render/LightSelection.h>

#include <cstdint>
#include <vector>

class EditorDocument;

struct EditorLightGather
{
    std::vector<ForwardLightCandidate> Candidates;
    std::uint64_t ContentHash = 0;
};

// Applies editor-only visibility filtering while producing the engine-owned
// candidate type. Baked-direct lights are gathered like every other light --
// the candidate carries the baked flag, and charted receivers skip them in
// the shader exactly as the runtime does, so the lightmap preview never
// double-counts. Ranking, budget selection, GPU packing, and shadow request
// construction happen later in SelectForwardLights.
[[nodiscard]] EditorLightGather GatherEditorLights(
    const EditorDocument& document,
    float globalShadowSoftness);
