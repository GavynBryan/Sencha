#include "SelectionContext.h"

#include <algorithm>
#include <cassert>

std::span<const SelectableRef> SelectionContext::GetSelection() const
{
    return Selection;
}

SelectableRef SelectionContext::GetPrimarySelection() const
{
    return PrimarySelection;
}

bool SelectionContext::Contains(SelectableRef selection) const
{
    return std::find(Selection.begin(), Selection.end(), selection) != Selection.end();
}

SelectionSnapshot SelectionContext::GetSnapshot() const
{
    return SelectionSnapshot{
        .Items = Selection,
        .Primary = PrimarySelection,
    };
}

void SelectionContext::SetSnapshot(SelectionSnapshot snapshot)
{
    Selection.clear();
    Selection.reserve(snapshot.Items.size());

    for (SelectableRef ref : snapshot.Items)
    {
        if (!ref.IsValid() || Contains(ref))
            continue;
        assert((!ExpectedRegistry.IsValid() || ref.Registry == ExpectedRegistry)
               && "SelectionContext: ref from a non-focus registry; only the focus document is selectable");
        Selection.push_back(ref);
    }

    if (snapshot.Primary.IsValid() && Contains(snapshot.Primary))
        PrimarySelection = snapshot.Primary;
    else if (!Selection.empty())
        PrimarySelection = Selection.back();
    else
        PrimarySelection = {};
}
