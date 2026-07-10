#pragma once

#include "selection/SelectableRef.h"

#include <vector>

struct BrushMesh;

// Shortest topological path between two same-kind elements of one brush (the
// Ctrl+click gesture): vertices walk edges, edges step across shared vertices,
// faces step across shared edges. Breadth-first with index-ordered neighbor
// expansion, so ties resolve deterministically. The result includes both
// endpoints, ordered from `from` to `to`. Empty when the refs differ in kind or
// entity, either end does not resolve on the mesh, or no path exists.
[[nodiscard]] std::vector<SelectableRef> GatherPathSelection(const BrushMesh& mesh,
                                                             const SelectableRef& from,
                                                             const SelectableRef& to);
