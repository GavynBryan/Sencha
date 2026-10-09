#pragma once

#include "tools/RadialMenuModel.h"

#include "actions/ActionCatalog.h"

#include <functional>
#include <string>
#include <vector>

class WorkspaceHost;

// What can be made new, as a wheel: every offered workspace kind's create
// actions; choosing one has the host perform it in that kind's workspace
// (WorkspaceHost::RequestCreate), and `chosen` is told.
class NewContentWheel final : public IRadialMenuModel
{
public:
    NewContentWheel(WorkspaceHost& workspaces, std::function<void()> chosen);

    [[nodiscard]] int Count() const override { return static_cast<int>(Entries.size()); }
    [[nodiscard]] MenuItem Item(int index) const override;
    [[nodiscard]] int ActiveIndex() const override { return -1; }
    void Select(int index, int variant) override;
    void Opening(Vec2d pointer) override;

private:
    struct Entry
    {
        std::string Kind;
        const ActionDefinition* Action;
    };

    WorkspaceHost& Workspaces;
    std::function<void()> Chosen;
    std::vector<Entry> Entries;
};
