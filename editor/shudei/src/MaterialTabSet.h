#pragma once

#include "MaterialEditSession.h"

#include "commands/CommandStack.h"

#include <render/Material.h>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// One open material: its edit session, its own undo history, the resident
// preview handle (owned by the composition root, which loads/releases it),
// and the last session version pushed into the material cache.
struct MaterialEditTab
{
    MaterialEditSession Session;
    CommandStack Commands;
    MaterialHandle Handle{};
    uint64_t AppliedVersion = 0;
};

//=============================================================================
// MaterialTabSet
//
// The set of open materials plus which one is active. Pure tab bookkeeping
// over MaterialEditSession (headless-testable); asset residency stays with
// the composition root via each tab's Handle.
//=============================================================================
class MaterialTabSet
{
public:
    // Focuses the existing tab for virtualPath, or opens filePath in a new tab
    // (which becomes active). Null with *error set on parse failure.
    MaterialEditTab* OpenOrFocus(std::string virtualPath, std::string filePath,
                                 std::string* error);

    // Removes the tab; the caller releases its Handle first. The active tab
    // clamps to a neighbor.
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
