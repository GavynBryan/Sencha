#include "workspace/SelectionSummary.h"

#include "document/EntityLabel.h"
#include "workspace/EditorWorkspace.h"
#include "workspace/ElementVerbs.h"
#include "workspace/EntityVerbs.h"
#include "workspace/FaceUvSource.h"
#include "workspace/LightingDiagnostics.h"
#include "workspace/PartitionOutline.h"

#include <algorithm>
#include <format>

namespace
{
std::string Counted(std::size_t count, std::string_view one, std::string_view many)
{
    return std::format("{} {}", count, count == 1 ? one : many);
}
} // namespace

WorkspaceSelectionFacet SelectionOf(EditorWorkspace& workspace, ElementVerbs& verbs,
                                    const EntityVerbs& entityVerbs, FaceUvSource& uv, IPropertySource& components,
                                    PartitionOutline& partition, LightingDiagnostics& lighting)
{
    const std::span<const SelectableRef> selected = workspace.Selection.GetSelection();
    if (selected.empty())
    {
        WorkspaceSelectionFacet zone{ .Title = partition.Title() };
        if (!zone.Title.empty())
            zone.Sections.push_back({ .Heading = {}, .Source = &partition });
        return zone;
    }

    WorkspaceSelectionFacet facet;
    facet.Actions = verbs.Relevant();
    facet.Staged = verbs.HasPending();
    if (!verbs.Nodes().empty())
        facet.Sections.push_back({ .Heading = "Settings", .Source = &verbs });

    const auto countOf = [&](auto is) {
        return static_cast<std::size_t>(std::ranges::count_if(selected, is));
    };
    const MeshElementKind kind = workspace.MeshEdit.GetElementKind();
    std::size_t elements = 0;
    switch (kind)
    {
    case MeshElementKind::Face:
        elements = countOf([](const SelectableRef& ref) { return ref.IsFace(); });
        facet.Title = Counted(elements, "face", "faces");
        break;
    case MeshElementKind::Edge:
        elements = countOf([](const SelectableRef& ref) { return ref.IsEdge(); });
        facet.Title = Counted(elements, "edge", "edges");
        break;
    case MeshElementKind::Vertex:
        elements = countOf([](const SelectableRef& ref) { return ref.IsVertex(); });
        facet.Title = Counted(elements, "vertex", "vertices");
        break;
    case MeshElementKind::Object:
    default:
        break;
    }

    // Objects, or objects held while an element mode waits for elements to be
    // picked on them: named for the objects.
    if (elements == 0)
    {
        const std::size_t objects = countOf([](const SelectableRef& ref) { return ref.IsEntity(); });
        if (objects == 0)
            return {};
        const auto one = std::ranges::find_if(selected, [](const SelectableRef& ref) { return ref.IsEntity(); });
        facet.Title = objects == 1
            ? EntityLabel(workspace.World.FocusDocument(), one->Entity)
            : Counted(objects, "object", "objects");
        if (objects == 1)
        {
            if (!lighting.Nodes().empty())
                facet.Sections.push_back({ .Heading = "Lighting", .Source = &lighting });
            facet.Details.push_back({ .Heading = {}, .Source = &components });
        }
        if (!facet.Staged)
            for (std::string& verb : entityVerbs.Relevant())
                facet.Actions.push_back(std::move(verb));
        return facet;
    }

    if (kind == MeshElementKind::Face)
    {
        if (!facet.Staged)
            for (const char* action : { "uv.fit", "uv.center", "uv.reset_axes", "material.copy_proj", "material.paste_proj" })
                facet.Actions.emplace_back(action);
        if (!uv.Nodes().empty())
            facet.Sections.push_back({ .Heading = "Texture", .Source = &uv });
    }
    return facet;
}
