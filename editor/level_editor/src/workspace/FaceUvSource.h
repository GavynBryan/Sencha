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
//
// The selected faces' surface as properties: the material on them and how
// its texture is projected -- the basis it aligns to, scale, shift and
// rotation -- read from the first selected face and written to every one as
// one step per brush. The justify verbs (fit,
// center, reset axes) are actions in the Texture category; "treat as one"
// decides whether they justify each face or the selection as a single unit.
//=============================================================================
class FaceUvSource final : public IPropertySource
{
public:
    // `library` names the materials a face can take; null offers none.
    FaceUvSource(EditorWorkspace& workspace, const MaterialLibrary* library) : Workspace(workspace), Library(library) {}

    void Offer(ActionCatalog& catalog, ActionContext& context);
    // The texture verbs a face selection offers, in the order a person meets them.
    [[nodiscard]] std::vector<std::string> Relevant() const;

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    bool BeginEdit(std::string_view key) override;
    PropertyEditResult PreviewEdit(std::string_view key, std::string_view value) override;
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;
    void CancelEdit(std::string_view key) override;
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
