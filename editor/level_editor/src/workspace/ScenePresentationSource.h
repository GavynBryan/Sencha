#pragma once

#include "properties/PropertySource.h"

#include <vector>

class EditorWorkspace;

// A scene's own presentation settings, over the game's: what a level that is
// not a world shows with nothing selected. A world's zones show theirs under
// the partition outline instead. Each edit is one undoable step.
class ScenePresentationSource final : public CommitOnlyPropertySource
{
public:
    explicit ScenePresentationSource(EditorWorkspace& workspace) : Workspace(workspace) {}

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;

private:
    EditorWorkspace& Workspace;
};
