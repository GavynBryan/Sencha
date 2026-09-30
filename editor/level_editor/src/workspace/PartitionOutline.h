#pragma once

#include "problems/ProblemList.h"
#include "properties/PropertySource.h"
#include "workspaces/WorkspaceFacets.h"

#include <zone/WorldPartitionIds.h>
#include <zone/ZoneId.h>

#include <string>
#include <string_view>
#include <vector>

class EditorWorkspace;

//=============================================================================
// PartitionOutline
//
// A world's partition as an outline and as properties: the world scene, its
// graphs and the zones in each. Choosing a zone selects it; choosing a graph
// or the world picks it; putting a zone or the world to use focuses it. What
// is picked is presented as properties -- a zone's name, graph and
// visibility, a graph's name and streaming shape, the world's streaming
// preview -- and its verbs: focusing, loading and unloading a zone, taking its
// bounds from its contents, linking it to another zone, adding a zone to a
// graph or a graph to the world. Every manifest edit is one undoable step.
// Nothing here exists for a level that is not a world.
//=============================================================================
class PartitionOutline final : public IPropertySource
{
public:
    static constexpr std::string_view kId = "zones";

    explicit PartitionOutline(EditorWorkspace& workspace) : Workspace(workspace) {}

    [[nodiscard]] static WorkspaceExplorerInfo Info();
    [[nodiscard]] ExplorerPage Explore(const ExplorerQuery& query) const;
    void Choose(std::string_view key, ExplorerChoice choice, std::size_t mark);

    // What is picked, and what to call it; empty when nothing is.
    [[nodiscard]] std::string Title() const;
    // The world's validation as problems, each located by what it is about
    // ("zone:<id>", "graph:<id>", "transition:<id>").
    [[nodiscard]] std::vector<Problem> Problems(const DocumentRef& document) const;
    // Picks what a problem's location names; false when it names nothing here.
    bool Reveal(std::string_view location);

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    bool BeginEdit(std::string_view key) override;
    PropertyEditResult PreviewEdit(std::string_view key, std::string_view value) override;
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;
    void CancelEdit(std::string_view key) override;
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
