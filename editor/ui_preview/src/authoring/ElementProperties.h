#pragma once

#include "properties/PropertySource.h"

#include <ui/UiElementInfo.h>

#include <functional>

class UiPreviewSession;

// A picked element of the previewed document: its boxes, the computed values
// layout turns on, what it likely gets wrong, and its attributes. Read-only.
class ElementProperties final : public IPropertySource
{
public:
    ElementProperties(UiPreviewSession& session, std::function<UiElementRef()> element)
        : Session(session)
        , Element(std::move(element))
    {
    }

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    bool BeginEdit(std::string_view) override { return false; }
    PropertyEditResult PreviewEdit(std::string_view, std::string_view) override { return PropertyEditResult::Refused; }
    PropertyEditResult CommitEdit(std::string_view, std::string_view) override { return PropertyEditResult::Refused; }
    void CancelEdit(std::string_view) override {}

private:
    [[nodiscard]] std::vector<PropertyNode> Notes(const UiElementInfo& element) const;

    UiPreviewSession& Session;
    std::function<UiElementRef()> Element;
};
