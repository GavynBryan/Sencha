#pragma once

#include "ISelectionContext.h"

#include <functional>
#include <memory>
#include <span>
#include <vector>

class World;

//=============================================================================
// SelectionService
//
// The selection's mutation API, and the one place entity identity is settled.
//
// Every ref that enters the selection is normalized against the document it
// addresses: a ref with no persistent id is stamped with one, and a ref that
// carries a persistent id has its live handle re-resolved from it. That second
// rule is what makes the selection survive a scene-projection rebuild --
// placing, removing, or breaking an instance destroys and recreates entities,
// so a snapshot a command captured for undo comes back holding dead handles
// and heals on the way in. A ref whose identity no longer resolves is dropped
// rather than restored, because the entity it named is gone.
//
// Without a bound document the service behaves exactly as it did before
// identity existed: refs pass through on their raw handles.
//=============================================================================
class SelectionService
{
public:
    using ObserverFn = std::function<void(const SelectionSnapshot&)>;

    explicit SelectionService(ISelectionContext& context);

    // The document the selection addresses, for identity stamping and
    // resolution. Null unbinds (headless hosts and tests that select in a
    // world with no identity index). Set on focus change, beside the
    // selection clear that already happens there.
    void BindDocument(const World* world);

    // Re-resolves the live selection against the bound document, dropping
    // whatever no longer resolves. Call after anything that rebuilds entity
    // storage underneath a selection that is not itself being replaced.
    void RetargetToDocument();

    [[nodiscard]] std::span<const SelectableRef> GetSelection() const;
    [[nodiscard]] SelectableRef GetPrimarySelection() const;
    [[nodiscard]] SelectionSnapshot GetSnapshot() const;
    [[nodiscard]] bool Contains(SelectableRef selection) const;

    void SetSelection(std::vector<SelectableRef> selection);
    void AddSelection(SelectableRef selection);
    void ToggleSelection(SelectableRef selection);
    void RemoveSelection(SelectableRef selection);
    void ApplySelection(SelectableRef selection);
    void ApplySnapshot(SelectionSnapshot snapshot);
    void ClearSelection();

    // Drops every per-element (vertex/edge/face) selection, keeping object-level
    // (entity) selections. Called after a structural mesh edit, where element
    // indices shift or reindex so a kept element ref would resolve to the wrong
    // element (or none).
    void ClearMeshElementSelections();

    [[nodiscard]] ISelectionContext& GetContext();
    [[nodiscard]] const ISelectionContext& GetContext() const;

    [[nodiscard]] std::shared_ptr<ObserverFn> Subscribe(ObserverFn fn);

private:
    ISelectionContext& Context;
    const World* Document = nullptr;
    std::vector<std::weak_ptr<ObserverFn>> Observers;

    // Stamps identity onto fresh refs and re-resolves handles on identified
    // ones. A ref whose identity is gone comes back invalid, which the
    // context's own filter then drops.
    [[nodiscard]] SelectableRef Normalize(SelectableRef ref) const;
    // Normalize every item, then hand the snapshot to the context. The one
    // write path: every mutator below routes through it.
    void Commit(SelectionSnapshot snapshot);
    void Notify();
};
