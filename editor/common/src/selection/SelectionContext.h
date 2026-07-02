#pragma once

#include "ISelectionContext.h"

class SelectionContext : public ISelectionContext
{
public:
    std::span<const SelectableRef> GetSelection() const override;
    SelectableRef GetPrimarySelection() const override;
    bool Contains(SelectableRef selection) const override;
    SelectionSnapshot GetSnapshot() const override;
    void SetSnapshot(SelectionSnapshot snapshot) override;

    // Debug guard for multi-registry workspaces: once set, every ref entering
    // the selection must carry this registry id (only the focus document is
    // selectable; context zones are render-and-reference only). Invalid means
    // unchecked (single-registry hosts).
    void SetExpectedRegistry(RegistryId registry) { ExpectedRegistry = registry; }

private:
    std::vector<SelectableRef> Selection;
    SelectableRef PrimarySelection = {};
    RegistryId ExpectedRegistry = RegistryId::Invalid();
};
