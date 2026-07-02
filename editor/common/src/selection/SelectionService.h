#pragma once

#include "ISelectionContext.h"

#include <functional>
#include <memory>
#include <span>
#include <vector>

class SelectionService
{
public:
    using ObserverFn = std::function<void(const SelectionSnapshot&)>;

    explicit SelectionService(ISelectionContext& context);

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
    std::vector<std::weak_ptr<ObserverFn>> Observers;

    void Notify();
};
