#pragma once

#include "properties/PropertySource.h"

#include <string>
#include <vector>

class ActionCatalog;
class ActionContext;
class EditorWorkspace;

//=============================================================================
// FaceUvSource
//
// How the selected faces' textures are projected, as properties: the basis
// they align to, scale, shift and rotation, read from the first selected face
// and written to every one as one step per brush. The justify verbs (fit,
// center, reset axes) are actions in the Texture category; "treat as one"
// decides whether they justify each face or the selection as a single unit.
//=============================================================================
class FaceUvSource final : public IPropertySource
{
public:
    explicit FaceUvSource(EditorWorkspace& workspace) : Workspace(workspace) {}

    void Offer(ActionCatalog& catalog, ActionContext& context);

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    bool BeginEdit(std::string_view key) override;
    PropertyEditResult PreviewEdit(std::string_view key, std::string_view value) override;
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;
    void CancelEdit(std::string_view key) override;

private:
    PropertyEditResult Set(std::string_view key, std::string_view value);
    [[nodiscard]] std::string ValueOf(std::string_view key) const;
    [[nodiscard]] bool HasFaces() const;
    void Justify(bool fit);

    EditorWorkspace& Workspace;
    bool TreatAsOne = false;
};
