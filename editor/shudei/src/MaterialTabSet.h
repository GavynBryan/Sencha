#pragma once

#include "MaterialEditSession.h"

#include "commands/CommandStack.h"

#include <core/assets/AssetLease.h>
#include <render/Material.h>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// One open material: its edit session, its own undo history, the lease on
// the resident preview material (taken and let go by the composition root),
// and the last session version pushed into the material cache.
struct MaterialEditTab
{
    MaterialEditSession Session;
    CommandStack Commands;
    AssetLease Resident;
    uint64_t AppliedVersion = 0;

    [[nodiscard]] MaterialHandle Handle() const
    {
        return MaterialHandle::FromToken(Resident.OpaqueToken());
    }
};

//=============================================================================
// MaterialTabSet
//
// The set of open materials plus which one is active. Pure tab bookkeeping
// over MaterialEditSession (headless-testable); asset residency stays with
// the composition root via each tab's lease.
//=============================================================================
class MaterialTabSet
{
public:
    // Focuses the existing tab for virtualPath, or opens filePath in a new tab
    // (which becomes active). Null with *error set on parse failure.
    MaterialEditTab* OpenOrFocus(std::string virtualPath, std::string filePath,
                                 std::string* error);

    // Removes the tab, and its lease with it. The active tab clamps to a
    // neighbor.
    void Close(std::size_t index);

    [[nodiscard]] MaterialEditTab* Active();
    [[nodiscard]] std::size_t ActiveIndex() const { return ActiveTab; }
    void SetActive(std::size_t index);

    [[nodiscard]] MaterialEditTab* Find(std::string_view virtualPath);
    [[nodiscard]] const std::vector<std::unique_ptr<MaterialEditTab>>& Tabs() const { return List; }
    [[nodiscard]] std::vector<std::unique_ptr<MaterialEditTab>>& Tabs() { return List; }

    [[nodiscard]] bool AnyDirty() const;

    // Saves every dirty tab; returns how many saved. On failure keeps going
    // and reports the first error.
    int SaveAll(std::string* error);

private:
    std::vector<std::unique_ptr<MaterialEditTab>> List;
    std::size_t ActiveTab = 0;
};
