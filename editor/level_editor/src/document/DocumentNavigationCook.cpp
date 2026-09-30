#include "DocumentNavigationCook.h"

#include "CookArtifactPaths.h"
#include "CookArtifactTransaction.h"
#include "CookStepProgress.h"
#include "DocumentArtifactCatalog.h"
#include "DocumentCookContext.h"

#include <assets/cook/NavigationCook.h>
#include <assets/cook/StaticCollisionGeometry.h>
#include <navigation/NavigationFile.h>
#include <project/CookProfile.h>

#include <algorithm>
#include <fstream>
#include <string>
#include <system_error>

namespace
{
    // Stages the encoded file beside the cooked scene and records it.
    std::optional<CookedArtifact> StageNavigationFile(const DocumentCookContext& ctx,
                                                      const NavigationFile& navigation)
    {
        const std::vector<std::byte> bytes = EncodeNavigationFile(navigation);
        const std::string rel = NavigationRel(ctx.Stem());
        const std::filesystem::path staged = ctx.Transaction.Stage(ctx.AssetsRoot / ".cooked" / rel);
        std::error_code ec;
        std::filesystem::create_directories(staged.parent_path(), ec);
        std::ofstream stream(staged, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        if (!stream.good())
            return std::nullopt;
        return ctx.Catalog.AddNavigation("asset://" + rel, ".cooked/" + rel);
    }

    CookDiagnostic MissingSettingsWarning()
    {
        return CookDiagnostic{
            .Severity = CookDiagnosticSeverity::Warning,
            .Source = CookDiagnosticSource::NavigationSettings,
            .Rule = "nav.settings.missing",
            .Message = "navigation links exist but the project has no navigation.settings "
                       "asset; no navigation was cooked",
        };
    }
}

bool CookDocumentNavigation(const DocumentCookContext& ctx,
                            const DocumentCookSnapshot& snapshot,
                            const std::vector<BrushCell>& cells,
                            std::optional<CookedArtifact>& navigationArtifact)
{
    DocumentCookResult& result = ctx.Result;
    ctx.Progress.Begin(CookStepIds::Navigation);
    const auto fail = [&result](std::string message)
    {
        result.Error = "CookDocument: " + std::move(message);
        return false;
    };

    std::vector<CookDiagnostic>& diagnostics = result.Diagnostics;
    diagnostics.insert(diagnostics.end(), snapshot.NavigationDiagnostics.begin(),
                       snapshot.NavigationDiagnostics.end());
    if (HasCookErrors(diagnostics))
        return fail("navigation settings are invalid");

    if (snapshot.Navigation.has_value())
    {
        const StaticCollisionGeometry geometry = CollectStaticCollisionGeometry(cells);
        const NavigationCookResult cooked = CookZoneNavigation(NavigationCookInput{
            .Positions = geometry.Positions,
            .Indices = geometry.Indices,
            .Settings = &*snapshot.Navigation,
            .AreaVolumes = {},
            .Links = snapshot.NavLinks,
        });
        diagnostics.insert(diagnostics.end(), cooked.Diagnostics.begin(), cooked.Diagnostics.end());
        result.NavigationTileCount = cooked.TileCount;
        result.NavigationPolygonCount = cooked.PolygonCount;
        if (cooked.HasErrors())
            return fail("navigation cook reported errors");
        if (cooked.Navigation)
        {
            navigationArtifact = StageNavigationFile(ctx, *cooked.Navigation);
            if (!navigationArtifact)
                return fail("could not write navigation '" + NavigationRel(ctx.Stem()) + "'");
        }
    }
    else if (!snapshot.NavLinks.empty())
    {
        diagnostics.push_back(MissingSettingsWarning());
    }
    ctx.Progress.Complete();
    return true;
}
