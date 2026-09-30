#include "workspaces/WorkspaceHost.h"

#include <algorithm>
#include <utility>

WorkspaceHost::WorkspaceHost(std::vector<WorkspaceKind> kinds, bool hasProject, PresentationId mainWindow)
    : Table(std::move(kinds))
    , HasProject(hasProject)
    , Main(mainWindow)
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

WorkspaceHost::Entry* WorkspaceHost::FindEntry(std::string_view kind)
{
    const auto it = std::find_if(Opened.begin(), Opened.end(), [&](const Entry& entry) { return entry.Kind->Id == kind; });
    return it != Opened.end() ? &*it : nullptr;
}

WorkspaceHost::Entry* WorkspaceHost::FindEntry(const WorkspaceKind* kind)
{
    return kind != nullptr ? FindEntry(kind->Id) : nullptr;
}

const WorkspaceKind* WorkspaceHost::ActiveKind() const
{
    return RecentlyActive.empty() ? nullptr : RecentlyActive.back();
}

IWorkspace* WorkspaceHost::Active() const
{
    const WorkspaceKind* kind = ActiveKind();
    return kind != nullptr ? Find(kind->Id) : nullptr;
}

const WorkspaceKind* WorkspaceHost::ActiveKindIn(PresentationId window) const
{
    for (auto it = RecentlyActive.rbegin(); it != RecentlyActive.rend(); ++it)
        if (WindowOf((*it)->Id) == window)
            return *it;
    return nullptr;
}

IWorkspace* WorkspaceHost::ActiveIn(PresentationId window) const
{
    const WorkspaceKind* kind = ActiveKindIn(window);
    return kind != nullptr ? Find(kind->Id) : nullptr;
}

PresentationId WorkspaceHost::WindowOf(std::string_view kind) const
{
    for (const Entry& entry : Opened)
        if (entry.Kind->Id == kind)
            return entry.Window;
    return {};
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
    Opened.push_back(Entry{ row, std::move(instance), Main });
    if (OnOpened)
        OnOpened(*row, *built);
    (void)Activate(kind);
    return built;
}

bool WorkspaceHost::Activate(std::string_view kind)
{
    Entry* entry = FindEntry(kind);
    if (entry == nullptr)
        return false;
    const WorkspaceKind* previous = ActiveKindIn(entry->Window);
    std::erase(RecentlyActive, entry->Kind);
    RecentlyActive.push_back(entry->Kind);
    if (previous == entry->Kind)
        return true;
    if (Entry* shown = FindEntry(previous))
        shown->Instance->SetVisible(false);
    entry->Instance->SetVisible(true);
    return true;
}

bool WorkspaceHost::Place(std::string_view kind, PresentationId window)
{
    Entry* entry = FindEntry(kind);
    if (entry == nullptr || window.IsNull())
        return false;
    if (entry->Window == window)
        return Activate(kind);

    const PresentationId from = entry->Window;
    const bool wasShowing = ActiveKindIn(from) == entry->Kind;
    const WorkspaceKind* shownThere = ActiveKindIn(window);
    // A move interrupts whatever the workspace had in flight, as hiding does.
    if (wasShowing)
        entry->Instance->SetVisible(false);
    entry->Window = window;
    std::erase(RecentlyActive, entry->Kind);
    RecentlyActive.push_back(entry->Kind);
    if (Entry* covered = FindEntry(shownThere))
        covered->Instance->SetVisible(false);
    if (OnPlaced)
        OnPlaced(*entry->Kind, *entry->Instance);
    entry->Instance->SetVisible(true);
    if (wasShowing)
        SettleWindow(from);
    return true;
}

bool WorkspaceHost::Detach(std::string_view kind)
{
    Entry* entry = FindEntry(kind);
    if (entry == nullptr || !OpenWindow)
        return false;
    const PresentationId window = OpenWindow(*entry->Kind);
    return !window.IsNull() && Place(kind, window);
}

void WorkspaceHost::Gather(PresentationId window)
{
    std::vector<std::string> placed;
    for (const Entry& entry : Opened)
        if (entry.Window == window)
            placed.push_back(entry.Kind->Id);
    for (const std::string& kind : placed)
        (void)Place(kind, Main);
    if (placed.empty() && window != Main && OnWindowEmptied)
        OnWindowEmptied(window);
}

void WorkspaceHost::SettleWindow(PresentationId window)
{
    if (IWorkspace* next = ActiveIn(window))
        next->SetVisible(true);
    else if (window != Main && OnWindowEmptied)
        OnWindowEmptied(window);
}

bool WorkspaceHost::Close(std::string_view kind)
{
    Entry* entry = FindEntry(kind);
    if (entry == nullptr)
        return false;
    if (MayClose && !MayClose(*entry->Kind, *entry->Instance))
        return false;
    return Destroy(kind);
}

bool WorkspaceHost::Destroy(std::string_view kind)
{
    const auto it = std::find_if(Opened.begin(), Opened.end(), [&](const Entry& entry) { return entry.Kind->Id == kind; });
    if (it == Opened.end())
        return false;

    const WorkspaceKind* row = it->Kind;
    const PresentationId window = it->Window;
    const bool wasShowing = ActiveKindIn(window) == row;
    if (OnClosing)
        OnClosing(*row, *it->Instance);
    // Out of the list before it is destroyed, so nothing it runs on the way
    // out finds itself still open.
    std::unique_ptr<IWorkspace> closing = std::move(it->Instance);
    Opened.erase(it);
    std::erase(RecentlyActive, row);
    closing.reset();

    if (wasShowing)
        SettleWindow(window);
    return true;
}

void WorkspaceHost::CloseAll()
{
    // Nothing takes over from a workspace closing along with all the others.
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
        case WorkspaceAction::Detach:   (void)Detach(request.Kind);   break;
        case WorkspaceAction::Attach:   (void)Attach(request.Kind);   break;
        }
    }
}

void WorkspaceHost::Tick(FrameUpdateContext& ctx)
{
    ApplyRequests();
    for (const Entry& entry : Opened)
        entry.Instance->Tick(ctx);
}
