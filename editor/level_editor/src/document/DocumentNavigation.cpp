#include "DocumentNavigation.h"

#include "BrushCookInput.h"
#include "EditorDocument.h"
#include "NavigationCookInput.h"

#include <assets/cook/BrushClustering.h>
#include <assets/cook/NavigationCook.h>
#include <assets/cook/StaticCollisionGeometry.h>

DocumentNavigation BuildDocumentNavigation(const EditorDocument& document,
                                           const std::filesystem::path& assetsRoot,
                                           double cellSize)
{
    DocumentNavigation result;
    NavigationSettingsLookup settings = FindNavigationSettings(assetsRoot);
    result.Diagnostics = std::move(settings.Diagnostics);
    if (!settings.Settings)
        return result;

    const std::vector<CookBrushGeometry> brushes =
        CollectCookBrushes(document.GetScene(), document.GetDefaultMaterial());
    const std::vector<BrushCell> cells = ClusterBrushesIntoCells(brushes, cellSize);
    const StaticCollisionGeometry geometry = CollectStaticCollisionGeometry(cells);
    const std::vector<NavLinkRecord> links = CollectNavLinks(document);
    NavigationCookResult cooked = CookZoneNavigation(NavigationCookInput{
        .Positions = geometry.Positions,
        .Indices = geometry.Indices,
        .Settings = &*settings.Settings,
        .AreaVolumes = {},
        .Links = links,
    });
    result.Diagnostics.insert(result.Diagnostics.end(), cooked.Diagnostics.begin(),
                              cooked.Diagnostics.end());
    if (!cooked.HasErrors())
        result.File = std::move(cooked.Navigation);
    return result;
}
