#include "shell/AssetTabs.h"

#include "workspaces/IWorkspace.h"
#include "workspaces/WorkspaceHost.h"

std::vector<AssetTab> AssetTabsIn(const WorkspaceHost& host, PresentationId window)
{
    std::vector<AssetTab> tabs;
    const WorkspaceKind* showing = host.ActiveKindIn(window);
    for (const WorkspaceKind& kind : host.Kinds())
    {
        const IWorkspace* workspace = host.Find(kind.Id);
        if (workspace == nullptr || host.WindowOf(kind.Id) != window)
            continue;
        const WorkspaceDocumentFacet facet = workspace->Document();
        const bool active = showing != nullptr && showing->Id == kind.Id;
        if (facet.Open.empty())
            tabs.push_back({ .Kind = kind.Id,
                             .KindLabel = kind.DisplayName,
                             .Document = {},
                             .Label = facet.Label.empty() ? kind.DisplayName : facet.Label,
                             .Changed = facet.Changed,
                             .Current = active });
        for (const WorkspaceOpenDocument& document : facet.Open)
            tabs.push_back({ .Kind = kind.Id,
                             .KindLabel = kind.DisplayName,
                             .Document = document.Key,
                             .Label = document.Label,
                             .Changed = document.Changed,
                             .Current = active && document.Key == facet.Active });
    }
    return tabs;
}
