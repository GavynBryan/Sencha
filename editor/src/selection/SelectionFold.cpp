#include "SelectionFold.h"

#include <algorithm>

namespace
{
bool Contains(const std::vector<SelectableRef>& items, SelectableRef ref)
{
    return std::find(items.begin(), items.end(), ref) != items.end();
}

void RemoveAll(SelectionSnapshot& out, const std::vector<SelectableRef>& doomed)
{
    out.Items.erase(std::remove_if(out.Items.begin(), out.Items.end(),
                                   [&](SelectableRef r) { return Contains(doomed, r); }),
                    out.Items.end());
    if (!Contains(out.Items, out.Primary))
        out.Primary = out.Items.empty() ? SelectableRef{} : out.Items.back();
}
}

SelectionSnapshot SelectionFold::Apply(const SelectionSnapshot& current,
                                       const std::vector<SelectableRef>& gathered,
                                       Op op)
{
    SelectionSnapshot out;
    switch (op)
    {
    case Op::Remove:
        out = current;
        RemoveAll(out, gathered);
        break;

    case Op::Add:
        out = current;
        for (SelectableRef ref : gathered)
            if (ref.IsValid() && !Contains(out.Items, ref))
                out.Items.push_back(ref);
        // The clicked/gathered item becomes primary even when it was already
        // selected (re-click promotes).
        for (auto it = gathered.rbegin(); it != gathered.rend(); ++it)
            if (it->IsValid())
            {
                out.Primary = *it;
                break;
            }
        break;

    case Op::Toggle:
    {
        out = current;
        std::vector<SelectableRef> doomed;
        SelectableRef lastAdded;
        for (SelectableRef ref : gathered)
        {
            if (!ref.IsValid())
                continue;
            if (Contains(current.Items, ref))
            {
                doomed.push_back(ref);
            }
            else if (!Contains(out.Items, ref))
            {
                out.Items.push_back(ref);
                lastAdded = ref;
            }
        }
        RemoveAll(out, doomed);
        if (lastAdded.IsValid())
            out.Primary = lastAdded;
        break;
    }

    case Op::Replace:
        for (SelectableRef ref : gathered)
            if (ref.IsValid() && !Contains(out.Items, ref))
                out.Items.push_back(ref);
        out.Primary = out.Items.empty() ? SelectableRef{} : out.Items.back();
        break;
    }
    return out;
}

SelectionFold::Op SelectionFold::OpForClick(bool ctrl, bool shift)
{
    if (ctrl && shift)
        return Op::Remove;
    if (ctrl)
        return Op::Toggle;
    if (shift)
        return Op::Add;
    return Op::Replace;
}

SelectionFold::Op SelectionFold::OpForBulk(bool ctrl, bool shift)
{
    if (ctrl)
        return Op::Remove;
    if (shift)
        return Op::Add;
    return Op::Replace;
}
