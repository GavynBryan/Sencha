#pragma once

#include "workspaces/TreeOutline.h"
#include "workspaces/facets/ExplorerFacet.h"

#include <core/identity/Id.h>

#include <cstddef>
#include <vector>

class EditorWorkspace;

// The scene being edited as outline nodes: every entity under its parent,
// named, keyed by its persistent id (so a branch keeps its state through undo
// and reload), with its visibility and lock as marks. A placement reads as one
// thing until it is opened. Choosing one selects it, alone, as a range across
// the outline or beside the others; a mark hides or locks it as one step.
class WorldExplorer
{
public:
    explicit WorldExplorer(EditorWorkspace& workspace) : Workspace(workspace) {}

    // The nodes, `depth` below where the outline puts the scene.
    [[nodiscard]] std::vector<OutlineNode> OutlineNodes(int depth);
    void Choose(PersistentEntityId entity, ExplorerChoice choice, std::size_t mark);

private:
    EditorWorkspace& Workspace;
    // The entities in the order last listed, which a range selects across.
    std::vector<PersistentEntityId> Listed;
};
