#pragma once

#include "properties/PropertySource.h"

#include <ui/UiElementInfo.h>

#include <functional>

class UiPreviewSession;

// A picked element of the previewed document: its boxes, the computed values
// layout turns on, what it likely gets wrong, and its attributes. Read-only.
class ElementProperties final : public ReadOnlyPropertySource
{
public:
    ElementProperties(UiPreviewSession& session, std::function<UiElementRef()> element)
        : Session(session)
        , Element(std::move(element))
    {
    }

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;

private:
    [[nodiscard]] std::vector<PropertyNode> Notes(const UiElementInfo& element) const;

    UiPreviewSession& Session;
    std::function<UiElementRef()> Element;
};
