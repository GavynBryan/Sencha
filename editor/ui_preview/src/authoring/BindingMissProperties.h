#pragma once

#include "documents/SnapshotDocument.h"
#include "properties/PropertySource.h"

class UiPreviewSession;

// What the open document asked its model for and the model does not declare
// (BindingMisses), each with the declarations that would satisfy it as verbs.
// Declaring one is a step on `model`, the sidecar's document, and remakes the
// screen against the model.
class BindingMissProperties final : public IPropertySource
{
public:
    BindingMissProperties(UiPreviewSession& session, SnapshotDocument& model) : Session(session), Model(model) {}

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    bool BeginEdit(std::string_view) override { return false; }
    PropertyEditResult PreviewEdit(std::string_view, std::string_view) override { return PropertyEditResult::Refused; }
    PropertyEditResult CommitEdit(std::string_view, std::string_view) override { return PropertyEditResult::Refused; }
    void CancelEdit(std::string_view) override {}
    PropertyEditResult Act(std::string_view key, std::string_view verb, std::string_view choice) override;

private:
    UiPreviewSession& Session;
    SnapshotDocument& Model;
};
