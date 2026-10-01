#pragma once

#include "properties/PropertySource.h"

class UiPreviewSession;

//=============================================================================
// BindingMissProperties
//
// What the open document asked its model for and the model does not declare,
// observed so far (BindingMisses), each with the declarations that would
// satisfy it as verbs: a variable as a property, a list or row list, a member
// read through an undeclared list as a row list, an event as an action.
// Declaring one remakes the screen against the model.
//=============================================================================
class BindingMissProperties final : public IPropertySource
{
public:
    explicit BindingMissProperties(UiPreviewSession& session) : Session(session) {}

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    bool BeginEdit(std::string_view) override { return false; }
    PropertyEditResult PreviewEdit(std::string_view, std::string_view) override { return PropertyEditResult::Refused; }
    PropertyEditResult CommitEdit(std::string_view, std::string_view) override { return PropertyEditResult::Refused; }
    void CancelEdit(std::string_view) override {}
    PropertyEditResult Act(std::string_view key, std::string_view verb, std::string_view choice) override;

private:
    UiPreviewSession& Session;
};
