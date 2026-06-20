#include "SelectionFold.h"

#include <algorithm>

namespace
{
bool Contains(const std::vector<SelectableRef>& items, SelectableRef ref)
{
    return std::find(items.begin(), items.end(), ref) != items.end();
}
}

SelectionSnapshot SelectionFold::Apply(const SelectionSnapshot& current,
                                       const std::vector<SelectableRef>& gathered,
                                       bool add, bool remove)
{
    SelectionSnapshot out;
    if (remove)
    {
        out = current;
        out.Items.erase(std::remove_if(out.Items.begin(), out.Items.end(),
                                       [&](SelectableRef r) { return Contains(gathered, r); }),
                        out.Items.end());
        if (!Contains(out.Items, out.Primary))
            out.Primary = out.Items.empty() ? SelectableRef{} : out.Items.back();
    }
    else if (add)
    {
        out = current;
        for (SelectableRef ref : gathered)
            if (ref.IsValid() && !Contains(out.Items, ref))
                out.Items.push_back(ref);
        if (!gathered.empty())
            out.Primary = gathered.back();
    }
    else // replace
    {
        for (SelectableRef ref : gathered)
            if (ref.IsValid())
                out.Items.push_back(ref);
        out.Primary = out.Items.empty() ? SelectableRef{} : out.Items.back();
    }
    return out;
}
