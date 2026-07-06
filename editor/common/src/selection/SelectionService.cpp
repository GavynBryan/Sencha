#include "SelectionService.h"

#include <algorithm>

SelectionService::SelectionService(ISelectionContext& context)
    : Context(context)
{
}

std::span<const SelectableRef> SelectionService::GetSelection() const
{
    return Context.GetSelection();
}

SelectableRef SelectionService::GetPrimarySelection() const
{
    return Context.GetPrimarySelection();
}

SelectionSnapshot SelectionService::GetSnapshot() const
{
    return Context.GetSnapshot();
}

bool SelectionService::Contains(SelectableRef selection) const
{
    return Context.Contains(selection);
}

void SelectionService::SetSelection(std::vector<SelectableRef> selection)
{
    SelectableRef primary = {};
    if (!selection.empty())
        primary = selection.back();

    Context.SetSnapshot(SelectionSnapshot{
        .Items = std::move(selection),
        .Primary = primary,
    });
    Notify();
}

void SelectionService::AddSelection(SelectableRef selection)
{
    if (!selection.IsValid())
        return;

    SelectionSnapshot snapshot = Context.GetSnapshot();
    if (std::find(snapshot.Items.begin(), snapshot.Items.end(), selection) == snapshot.Items.end())
        snapshot.Items.push_back(selection);
    snapshot.Primary = selection;
    Context.SetSnapshot(std::move(snapshot));
    Notify();
}

void SelectionService::ToggleSelection(SelectableRef selection)
{
    if (!selection.IsValid())
        return;

    SelectionSnapshot snapshot = Context.GetSnapshot();
    const auto it = std::find(snapshot.Items.begin(), snapshot.Items.end(), selection);
    if (it == snapshot.Items.end())
    {
        snapshot.Items.push_back(selection);
        snapshot.Primary = selection;
    }
    else
    {
        snapshot.Items.erase(it);
        snapshot.Primary = snapshot.Items.empty() ? SelectableRef{} : snapshot.Items.back();
    }
    Context.SetSnapshot(std::move(snapshot));
    Notify();
}

void SelectionService::RemoveSelection(SelectableRef selection)
{
    SelectionSnapshot snapshot = Context.GetSnapshot();
    const auto it = std::find(snapshot.Items.begin(), snapshot.Items.end(), selection);
    if (it == snapshot.Items.end())
        return;

    snapshot.Items.erase(it);
    snapshot.Primary = snapshot.Items.empty() ? SelectableRef{} : snapshot.Items.back();
    Context.SetSnapshot(std::move(snapshot));
    Notify();
}

void SelectionService::ApplySelection(SelectableRef selection)
{
    if (!selection.IsValid())
    {
        ClearSelection();
        return;
    }

    Context.SetSnapshot(SelectionSnapshot{
        .Items = { selection },
        .Primary = selection,
    });
    Notify();
}

void SelectionService::ApplySnapshot(SelectionSnapshot snapshot)
{
    Context.SetSnapshot(std::move(snapshot));
    Notify();
}

void SelectionService::ClearSelection()
{
    Context.SetSnapshot({});
    Notify();
}

void SelectionService::ClearMeshElementSelections()
{
    const std::span<const SelectableRef> current = Context.GetSelection();
    std::vector<SelectableRef> kept;
    kept.reserve(current.size());
    for (const SelectableRef& ref : current)
    {
        if (!ref.IsMeshElement())
            kept.push_back(ref);
    }
    if (kept.size() == current.size())
        return; // no element selections to drop; don't churn observers

    SetSelection(std::move(kept));
}

ISelectionContext& SelectionService::GetContext()
{
    return Context;
}

const ISelectionContext& SelectionService::GetContext() const
{
    return Context;
}

std::shared_ptr<SelectionService::ObserverFn> SelectionService::Subscribe(ObserverFn fn)
{
    auto ptr = std::make_shared<ObserverFn>(std::move(fn));
    Observers.push_back(ptr);
    return ptr;
}

void SelectionService::Notify()
{
    const SelectionSnapshot snapshot = Context.GetSnapshot();
    auto it = Observers.begin();
    while (it != Observers.end())
    {
        if (auto fn = it->lock())
        {
            (*fn)(snapshot);
            ++it;
        }
        else
        {
            it = Observers.erase(it);
        }
    }
}
