#include "SelectionService.h"

#include <ecs/World.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>

#include <algorithm>
#include <utility>

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

    Commit(SelectionSnapshot{
        .Items = std::move(selection),
        .Primary = primary,
    });
}

void SelectionService::AddSelection(SelectableRef selection)
{
    if (!selection.IsValid())
        return;

    SelectionSnapshot snapshot = Context.GetSnapshot();
    if (std::find(snapshot.Items.begin(), snapshot.Items.end(), selection) == snapshot.Items.end())
        snapshot.Items.push_back(selection);
    snapshot.Primary = selection;
    Commit(std::move(snapshot));
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
    Commit(std::move(snapshot));
}

void SelectionService::RemoveSelection(SelectableRef selection)
{
    SelectionSnapshot snapshot = Context.GetSnapshot();
    const auto it = std::find(snapshot.Items.begin(), snapshot.Items.end(), selection);
    if (it == snapshot.Items.end())
        return;

    snapshot.Items.erase(it);
    snapshot.Primary = snapshot.Items.empty() ? SelectableRef{} : snapshot.Items.back();
    Commit(std::move(snapshot));
}

void SelectionService::ApplySelection(SelectableRef selection)
{
    if (!selection.IsValid())
    {
        ClearSelection();
        return;
    }

    Commit(SelectionSnapshot{
        .Items = { selection },
        .Primary = selection,
    });
}

void SelectionService::ApplySnapshot(SelectionSnapshot snapshot)
{
    Commit(std::move(snapshot));
}

void SelectionService::ClearSelection()
{
    Commit({});
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

void SelectionService::BindDocument(const World* world)
{
    Document = world;
}

void SelectionService::RetargetToDocument()
{
    if (Document == nullptr)
        return;

    // Re-committing the current snapshot normalizes it: handles re-resolve
    // from identity and anything gone is dropped. Silent when nothing moved,
    // so a rebuild that did not disturb the selection costs no observer churn.
    SelectionSnapshot current = Context.GetSnapshot();
    SelectionSnapshot retargeted = current;
    for (SelectableRef& ref : retargeted.Items)
        ref = Normalize(ref);
    retargeted.Primary = Normalize(retargeted.Primary);

    const bool moved =
        retargeted.Items.size() != current.Items.size()
        || !std::equal(retargeted.Items.begin(), retargeted.Items.end(),
                       current.Items.begin(),
                       [](const SelectableRef& a, const SelectableRef& b)
                       { return a.Entity == b.Entity && a.Stable == b.Stable; })
        || retargeted.Primary.Entity != current.Primary.Entity;
    if (!moved)
        return;

    Commit(std::move(retargeted));
}

SelectableRef SelectionService::Normalize(SelectableRef ref) const
{
    if (Document == nullptr || !ref.Registry.IsValid())
        return ref;

    if (ref.Stable.IsValid())
    {
        // Identity leads: the handle is whatever this document says it is now,
        // which is how a snapshot captured before a rebuild comes back live.
        const auto* index = Document->TryGetResource<PersistentEntityIndex>();
        ref.Entity = index != nullptr ? index->TryResolve(ref.Stable) : EntityId{};
        return ref;
    }

    // A fresh ref off a pick or a query: stamp the identity it will be
    // recognised by later. Entities a document does not identify keep their
    // handle and stay handle-compared.
    if (ref.Entity.IsValid())
    {
        if (const auto* id = Document->TryGet<PersistentIdComponent>(ref.Entity))
            ref.Stable = id->Id;
    }
    return ref;
}

void SelectionService::Commit(SelectionSnapshot snapshot)
{
    for (SelectableRef& ref : snapshot.Items)
        ref = Normalize(ref);
    snapshot.Primary = Normalize(snapshot.Primary);
    Context.SetSnapshot(std::move(snapshot));
    Notify();
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
