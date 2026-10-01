#pragma once

#include "workspaces/WorkspaceFacets.h"

#include <ui/UiElementInfo.h>

#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>

// `tag#id.class.class`, as a selector would name the element.
[[nodiscard]] std::string ElementLabel(const UiElementInfo& element);

//=============================================================================
// ElementOutline
//
// A laid-out document's elements as an outline: each under its parent, every
// branch open until closed, and the branches above the selected element opened
// whenever the selection moves to it. With a filter, an element stays when it
// or anything under it matches. Keys name an element by its ref, so a pick
// made on an outline the document has since rebuilt resolves to nothing.
//=============================================================================
class ElementOutline
{
public:
    // `tree` is preorder, as UiService::ElementTree returns it.
    [[nodiscard]] ExplorerPage Explore(std::span<const UiElementInfo> tree, UiElementRef selected,
                                       const ExplorerQuery& query);
    [[nodiscard]] static std::optional<UiElementRef> Resolve(std::span<const UiElementInfo> tree, std::string_view key);
    void Flip(UiElementRef element);

private:
    std::set<std::uint32_t> Closed;
    UiElementRef Revealed;
};
