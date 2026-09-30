#include "workspaces/WorkspaceHost.h"

#include <algorithm>
#include <utility>

WorkspaceHost::WorkspaceHost(std::vector<WorkspaceKind> kinds, bool hasProject)
    : Table(std::move(kinds))
    , HasProject(hasProject)
{
}

WorkspaceHost::~WorkspaceHost()
{
    CloseAll();
}

const WorkspaceKind* WorkspaceHost::FindKind(std::string_view kind) const
{
    const auto it = std::find_if(Table.begin(), Table.end(), [&](const WorkspaceKind& row) { return row.Id == kind; });
    return it != Table.end() ? &*it : nullptr;
}

bool WorkspaceHost::IsOffered(std::string_view kind) const
{
    const WorkspaceKind* row = FindKind(kind);
    return row != nullptr && (HasProject || !row->RequiresProject);
}

IWorkspace* WorkspaceHost::Find(std::string_view kind) const
{
    for (const Entry& entry : Opened)
        if (entry.Kind->Id == kind)
            return entry.Instance.get();
    return nullptr;
}

IWorkspace* WorkspaceHost::Active() const
{
    return ActiveEntryKind != nullptr ? Find(ActiveEntryKind->Id) : nullptr;
}

IWorkspace* WorkspaceHost::Open(std::string_view kind)
{
    if (IWorkspace* existing = Find(kind))
    {
        (void)Activate(kind);
        return existing;
    }
    if (!IsOffered(kind))
        return nullptr;
    const WorkspaceKind* row = FindKind(kind);
    std::unique_ptr<IWorkspace> instance = row->Create ? row->Create() : nullptr;
    if (instance == nullptr)
        return nullptr;

    IWorkspace* built = instance.get();
    Opened.push_back(Entry{ row, std::move(instance) });
    if (OnOpened)
        OnOpened(*row, *built);
    (void)Activate(kind);
    return built;
}

bool WorkspaceHost::Activate(std::string_view kind)
{
    const auto it = std::find_if(Opened.begin(), Opened.end(), [&](const Entry& entry) { return entry.Kind->Id == kind; });
    if (it == Opened.end())
        return false;
    if (ActiveEntryKind == it->Kind)
        return true;
    if (IWorkspace* previous = Active())
        previous->SetVisible(false);
    ActiveEntryKind = it->Kind;
    std::erase(RecentlyActive, it->Kind);
    RecentlyActive.push_back(it->Kind);
    it->Instance->SetVisible(true);
    return true;
}

bool WorkspaceHost::Close(std::string_view kind)
{
    const auto it = std::find_if(Opened.begin(), Opened.end(), [&](const Entry& entry) { return entry.Kind->Id == kind; });
    if (it == Opened.end())
        return false;
    if (MayClose && !MayClose(*it->Kind, *it->Instance))
        return false;
    return Destroy(kind);
}

bool WorkspaceHost::Destroy(std::string_view kind)
{
    const auto it = std::find_if(Opened.begin(), Opened.end(), [&](const Entry& entry) { return entry.Kind->Id == kind; });
    if (it == Opened.end())
        return false;

    const WorkspaceKind* row = it->Kind;
    if (OnClosing)
        OnClosing(*row, *it->Instance);
    // Out of the list before it is destroyed, so nothing it runs on the way
    // out finds itself still open.
    std::unique_ptr<IWorkspace> closing = std::move(it->Instance);
    Opened.erase(it);
    std::erase(RecentlyActive, row);
    const bool wasActive = ActiveEntryKind == row;
    if (wasActive)
        ActiveEntryKind = nullptr;
    closing.reset();

    if (wasActive && !RecentlyActive.empty())
        (void)Activate(RecentlyActive.back()->Id);
    return true;
}

void WorkspaceHost::CloseAll()
{
    // Nothing takes over from a workspace closing along with all the others.
    ActiveEntryKind = nullptr;
    RecentlyActive.clear();
    while (!Opened.empty())
        (void)Destroy(Opened.back().Kind->Id);
}

void WorkspaceHost::ApplyRequests()
{
    // Taken first: a workspace opening may itself request something.
    std::vector<WorkspaceRequest> requests = std::move(Pending);
    Pending.clear();
    for (const WorkspaceRequest& request : requests)
    {
        switch (request.Action)
        {
        case WorkspaceAction::Open:     (void)Open(request.Kind);     break;
        case WorkspaceAction::Close:    (void)Close(request.Kind);    break;
        case WorkspaceAction::Activate: (void)Activate(request.Kind); break;
        }
    }
}

void WorkspaceHost::Tick(FrameUpdateContext& ctx)
{
    ApplyRequests();
    for (const Entry& entry : Opened)
        entry.Instance->Tick(ctx);
}
