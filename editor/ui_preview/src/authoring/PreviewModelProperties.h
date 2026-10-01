#pragma once

#include "authoring/UiPreviewModel.h"

#include "properties/PropertySource.h"

#include <optional>

class UiPreviewSession;

//=============================================================================
// PreviewModelProperties
//
// The preview model the open document is shown against, as properties: its
// name and modality, each declared property with its kind, sample and whether
// a control may write it, each sample list and row list with its items, and
// each action. Samples publish to the open screen as they change; a change to
// what is declared remakes the screen against the new model, since a
// declaration is made before the document loads. Saving is the sidecar's.
//=============================================================================
class PreviewModelProperties final : public IPropertySource
{
public:
    explicit PreviewModelProperties(UiPreviewSession& session) : Session(session) {}

    // The model is what its sidecar holds: just opened, or just saved.
    void MarkSaved();
    [[nodiscard]] bool Changed() const;

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    bool BeginEdit(std::string_view key) override;
    PropertyEditResult PreviewEdit(std::string_view key, std::string_view value) override;
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;
    void CancelEdit(std::string_view key) override;
    PropertyEditResult Act(std::string_view key, std::string_view verb, std::string_view choice) override;

private:
    // What an edit touched: values the open screen can take as they are, or
    // what the screen was declared with.
    enum class Reach : std::uint8_t
    {
        Samples,
        Declaration,
    };
    struct Applied
    {
        PropertyEditResult Result = PropertyEditResult::Gone;
        Reach Touched = Reach::Samples;
    };

    [[nodiscard]] Applied Apply(std::string_view key, std::string_view value);
    PropertyEditResult Show(Applied applied);

    UiPreviewSession& Session;
    UiPreviewModel Saved;
    // The model as an edit found it, put back if the edit is cancelled.
    std::optional<UiPreviewModel> Before;
};
