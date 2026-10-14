#pragma once

#include "documents/SnapshotDocument.h"
#include "properties/PropertySource.h"

class UiPreviewSession;

// What the open document asked its model for and the model does not declare,
// each declaration that would satisfy it a verb; declaring is a step on `model`.
class BindingMissProperties final : public ReadOnlyPropertySource
{
public:
    BindingMissProperties(UiPreviewSession& session, SnapshotDocument& model) : Session(session), Model(model) {}

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    PropertyEditResult Act(std::string_view key, std::string_view verb, std::string_view choice) override;

private:
    UiPreviewSession& Session;
    SnapshotDocument& Model;
};
