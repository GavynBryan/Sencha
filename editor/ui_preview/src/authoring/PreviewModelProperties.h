#pragma once

#include "authoring/UiPreviewModel.h"

#include "documents/SnapshotDocument.h"
#include "properties/PropertySource.h"

#include <cstdint>

class UiPreviewSession;

// The session's model as its sidecar's text, for the journal.
[[nodiscard]] SnapshotValue PreviewModelSnapshots(UiPreviewSession& session);

// The preview model the open document is shown against, as properties: its
// name and modality, each declared property, sample list, row list and action.
// A sample publishes to the open screen; a change to what is declared remakes
// the screen, since a declaration is made before the document loads. Every
// edit is a step on `model`, the sidecar's document, and only while it is open.
class PreviewModelProperties final : public IPropertySource
{
public:
    PreviewModelProperties(UiPreviewSession& session, SnapshotDocument& model) : Session(session), Model(model) {}

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
    SnapshotDocument& Model;
};
