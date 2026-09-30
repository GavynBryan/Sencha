#pragma once

#include "DocumentCookSnapshot.h"

#include <assets/cook/BrushClustering.h>
#include <assets/cook/CookedCache.h>

#include <optional>
#include <vector>

struct DocumentCookContext;

// The navigation step: builds the zone's navigation file from the same static
// collision triangles the collision bake reads, stages it beside the cooked
// scene, and records its diagnostics on the result. Publishes nothing -- and
// succeeds -- when the project has no navigation settings or the zone has no
// geometry. Returns false only when the cook must stop.
[[nodiscard]] bool CookDocumentNavigation(const DocumentCookContext& ctx,
                                          const DocumentCookSnapshot& snapshot,
                                          const std::vector<BrushCell>& cells,
                                          std::optional<CookedArtifact>& navigationArtifact);
