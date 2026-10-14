#pragma once

#include "problems/ProblemList.h"
#include "properties/PropertySource.h"
#include "workspace/LevelLocation.h"
#include "workspaces/TreeOutline.h"
#include "workspaces/facets/ExplorerFacet.h"

#include <zone/WorldPartitionIds.h>
#include <zone/ZoneId.h>

#include <string>
#include <string_view>
#include <vector>

class EditorWorkspace;

//=============================================================================
// PartitionOutline
// A world's partition (the world, its graphs, their zones) as an outline, with
// the picked one's properties and verbs. Every manifest edit is one undoable
// step; a level that is not a world has none of this.
//=============================================================================
class PartitionOutline final : public CommitOnlyPropertySource
{
public:
    explicit PartitionOutline(EditorWorkspace& workspace) : Workspace(workspace) {}

    // The world, its graphs and their zones, as outline nodes.
    [[nodiscard]] std::vector<OutlineNode> OutlineNodes() const;
    void Choose(const LevelLocation& location, ExplorerChoice choice);

    // What is picked, and what to call it; empty when nothing is.
    [[nodiscard]] std::string Title() const;
    // The world's validation as problems, each located by what it is about.
    [[nodiscard]] std::vector<Problem> Problems(const DocumentRef& document) const;
    // Picks what a problem's location names; false when it names nothing here.
    bool Reveal(std::string_view location);

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;
    PropertyEditResult Act(std::string_view key, std::string_view verb, std::string_view choice) override;

private:
    enum class Picked : std::uint8_t
    {
        None,
        World,
        Graph,
        Zone,
    };

    [[nodiscard]] Picked What() const;
    [[nodiscard]] std::vector<PropertyNode> ZoneNodes(ZoneId zone) const;
    [[nodiscard]] std::vector<PropertyNode> GraphNodes(GraphId graph) const;
    [[nodiscard]] std::vector<PropertyNode> WorldNodes() const;
    [[nodiscard]] std::string ValueOf(std::string_view key) const;
    // Runs a manifest verb as one step; false when the verb changed nothing.
    template <typename Verb>
    bool Edit(Verb&& verb);

    EditorWorkspace& Workspace;
    // A graph or the world, picked; a zone picked is the world's selected zone.
    Picked Pick = Picked::None;
    GraphId PickedGraph{};
};
