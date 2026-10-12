#pragma once

#include "meshedit/MeshEditService.h"
#include "properties/PropertySource.h"

#include <functional>
#include <string>
#include <vector>

class ActionCatalog;
class ActionContext;
class EditorWorkspace;

//=============================================================================
// ElementVerbs
//
// What can be done to the selected brushes, faces, edges or vertices, as
// actions in the Selection category, and what the parametric ones are set to,
// as properties: inset distance, bevel width and segments, bridge segments,
// weld distance. While an inset, bevel or bridge is previewed, changing its
// setting regenerates the preview; element.apply and element.cancel resolve
// it.
//=============================================================================
class ElementVerbs final : public IPropertySource
{
public:
    // `exportGlb` shows the save dialog; the geometry comes from the workspace.
    ElementVerbs(EditorWorkspace& workspace, std::function<void()> exportGlb);

    void Offer(ActionCatalog& catalog, ActionContext& context);

    // The verbs that act on what is selected now, in the order a person meets
    // them.
    [[nodiscard]] std::vector<std::string> Relevant() const;

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    bool BeginEdit(std::string_view key) override;
    PropertyEditResult PreviewEdit(std::string_view key, std::string_view value) override;
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;
    void CancelEdit(std::string_view key) override;

private:
    struct Settings
    {
        float InsetDistance = 0.25f;
        float BevelWidth = 0.25f;
        int BevelSegments = 1;
        int BridgeSegments = 1;
        float WeldDistance = 0.1f;
    };

    PropertyEditResult Set(std::string_view key, std::string_view value);
    [[nodiscard]] std::string ValueOf(std::string_view key) const;
    // One undoable mesh edit over the selection. A verb that rebuilds the mesh
    // renumbers its elements, so the element selection goes with it.
    void Apply(MeshEditVerb verb, const MeshEditParams& params, bool renumbers);
    [[nodiscard]] std::size_t Selected(MeshElementKind kind) const;

    EditorWorkspace& Workspace;
    std::function<void()> ExportGlb;
    Settings Current;
    std::string EditKey;
    std::string EditBegan;
};
