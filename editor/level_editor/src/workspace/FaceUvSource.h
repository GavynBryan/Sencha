#pragma once

#include "properties/PropertySource.h"

#include <string>
#include <vector>

class ActionCatalog;
class ActionContext;
class EditorWorkspace;
class MaterialLibrary;

//=============================================================================
// FaceUvSource
// The selected faces' material and projection as properties, read from the first
// face and written to all as one step per brush; the justify verbs are Texture
// actions, applied per face or to the selection as one unit.
//=============================================================================
class FaceUvSource final : public CommitOnlyPropertySource
{
public:
    // `library` names the materials a face can take; null offers none.
    FaceUvSource(EditorWorkspace& workspace, const MaterialLibrary* library) : Workspace(workspace), Library(library) {}

    void Offer(ActionCatalog& catalog, ActionContext& context);
    // The texture verbs a face selection offers, in the order a person meets them.
    [[nodiscard]] std::vector<std::string> Relevant() const;

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;
    [[nodiscard]] std::vector<std::string> Candidates(std::string_view key) const override;

private:
    PropertyEditResult Set(std::string_view key, std::string_view value);
    [[nodiscard]] std::string ValueOf(std::string_view key) const;
    [[nodiscard]] bool HasFaces() const;
    void Justify(bool fit);

    EditorWorkspace& Workspace;
    const MaterialLibrary* Library = nullptr;
    bool TreatAsOne = false;
};
