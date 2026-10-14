#include "workspace/LevelBreadcrumb.h"

#include "document/EntityLabel.h"
#include "workspace/EditorWorkspace.h"
#include "workspace/HierarchyOutline.h"
#include "workspace/LevelLocation.h"
#include "workspace/WorldMapMode.h"

#include <algorithm>
#include <format>
#include <string>

WorkspaceLocationFacet LevelBreadcrumb(const EditorWorkspace& workspace, std::string_view documentLabel)
{
    const WorldDocument& world = workspace.World;
    WorkspaceLocationFacet facet{ .Parts = {},
                                  .Hierarchy = std::string(HierarchyOutline::kId),
                                  .MapAction = world.IsWorld() ? std::string(WorldMapMode::kAction) : std::string{} };
    if (world.IsWorld())
    {
        const WorldPartitionManifest& manifest = world.Manifest();
        facet.Parts.push_back({ .Label = manifest.Name.empty() ? std::string("World") : manifest.Name,
                                .Key = LevelLocationKey(WorldScenePlace{}) });
        // The zone the camera is in, or, outside every zone, the one being edited.
        const ZoneHeader* here = nullptr;
        if (const EditorViewport* view = workspace.Layout.Active())
            here = InnermostZoneAt(manifest, view->Camera.Position);
        if (here == nullptr)
            for (const ZoneHeader& zone : manifest.Zones)
                if (zone.Id == world.FocusZone())
                    here = &zone;
        if (here != nullptr)
        {
            facet.Parts.push_back({ .Label = GraphDisplayName(manifest, here->Graph),
                                    .Key = LevelLocationKey(here->Graph) });
            facet.Parts.push_back({ .Label = ZoneDisplayName(manifest, here->Id),
                                    .Key = LevelLocationKey(here->Id) });
        }
    }
    else if (!documentLabel.empty())
    {
        facet.Parts.push_back({ .Label = std::string(documentLabel), .Key = {} });
    }
    const SelectableRef primary = workspace.Selection.GetPrimarySelection();
    const EditorDocument& document = world.FocusDocument();
    const auto objects = std::ranges::count_if(workspace.Selection.GetSelection(), &SelectableRef::IsEntity);
    if (primary.IsEntity() && document.GetScene().HasEntity(primary.Entity))
        facet.Parts.push_back({ .Label = objects > 1 ? std::format("{} objects", objects)
                                                     : EntityLabel(document, primary.Entity),
                                .Key = LevelLocationKey(document.GetScene().PersistentIdOf(primary.Entity)),
                                .Selection = true });
    return facet;
}
