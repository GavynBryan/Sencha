#include "shell/NewContentWheel.h"

#include "workspaces/WorkspaceHost.h"

#include <utility>

NewContentWheel::NewContentWheel(WorkspaceHost& workspaces, std::function<void()> chosen)
    : Workspaces(workspaces)
    , Chosen(std::move(chosen))
{
}

IRadialMenuModel::MenuItem NewContentWheel::Item(int index) const
{
    if (index < 0 || index >= Count())
        return {};
    const ActionDefinition& action = *Entries[static_cast<std::size_t>(index)].Action;
    return { action.Label, action.Icon };
}

void NewContentWheel::Select(int index, int)
{
    if (index >= 0 && index < Count())
    {
        const Entry& entry = Entries[static_cast<std::size_t>(index)];
        if (Workspaces.RequestCreate(entry.Kind, entry.Action->Id) && Chosen)
            Chosen();
    }
}

void NewContentWheel::Opening(Vec2d)
{
    Entries.clear();
    for (const WorkspaceKind& kind : Workspaces.Kinds())
        if (Workspaces.IsOffered(kind.Id))
            for (const ActionDefinition& action : kind.Creates)
                Entries.push_back({ kind.Id, &action });
}
