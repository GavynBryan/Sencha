#pragma once

#include "properties/PropertySource.h"

#include <ui/UiElementInfo.h>

#include <functional>

class UiPreviewSession;

//=============================================================================
// ElementProperties
//
// What a picked element of the previewed document is: its depth, where it is
// and the size of its four boxes in surface pixels and in dp, the computed
// values a layout question usually turns on, what it is likely to get wrong
// (activatable but out of a controller's reach, a root that swallows every
// pointer event), and its attributes. Read-only: the document is edited in
// its own file.
//=============================================================================
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
