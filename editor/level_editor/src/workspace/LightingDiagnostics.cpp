#include "workspace/LevelLocation.h"
#include "workspace/LightingDiagnostics.h"

#include "actions/ActionOffer.h"
#include "commands/CommandStack.h"
#include "document/EntityLabel.h"
#include "document/LightingReadModel.h"
#include "properties/SettingProperties.h"
#include "render/ShadowResidencyReadout.h"
#include "selection/commands/SelectCommand.h"
#include "workspace/EditorWorkspace.h"

#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>

#include <bit>
#include <format>
#include <memory>

namespace
{
std::string_view PolicyLabel(ShadowUpdatePolicy policy)
{
    switch (policy)
    {
    case ShadowUpdatePolicy::EveryFrame: return "Every frame";
    case ShadowUpdatePolicy::OnChange:   return "On change";
    case ShadowUpdatePolicy::Static:     return "Static";
    }
    return "?";
}

std::string_view TypeLabel(GpuLightType type)
{
    return type == GpuLightType::Point ? "Point" : "Spot";
}

// "512 (496 usable)": the tile edge and the guard-banded interior the filter
// samples.
std::string TierLabel(std::uint32_t tile)
{
    return std::format("{} ({} usable)", tile, tile - 2u * kSpotShadowGuardTexels);
}

std::string StateOf(const ShadowResidencyReadout& readout, const ShadowResidencyReadout::LightRow& row)
{
    if (!row.Held)
        return "No shadow: over budget";
    if (row.Type == GpuLightType::Point)
    {
        const PointShadowSlotInfo& slot = readout.PointSlots[row.Slot];
        const int faces = static_cast<int>(kPointShadowFaceCount) - std::popcount(slot.PendingFaces);
        if (!slot.EverRendered)
            return std::format("Pending, {} of 6 faces", faces);
        if (slot.Invalid || slot.PendingFaces != 0)
            return std::format("Queued, {} of 6 faces", faces);
        return slot.FramesSinceRendered == 0 ? std::string("Rendered")
                                             : std::format("Cached {} frames", slot.FramesSinceRendered);
    }
    const SpotShadowSlotInfo& slot = readout.Slots[row.Slot];
    if (!slot.EverRendered)
        return "Pending";
    if (slot.Invalid)
        return "Queued";
    return slot.FramesSinceRendered == 0 ? std::string("Rendered")
                                         : std::format("Cached {} frames", slot.FramesSinceRendered);
}

double AtlasOccupancy(const ShadowResidencyReadout& readout)
{
    std::uint64_t used = 0;
    for (const SpotShadowSlotInfo& slot : readout.Slots)
        if (slot.Live && slot.Allocation.IsValid())
            used += static_cast<std::uint64_t>(slot.Allocation.Size) * slot.Allocation.Size;
    return 100.0 * static_cast<double>(used)
         / (static_cast<double>(kSpotShadowAtlasExtent) * kSpotShadowAtlasExtent);
}

PersistentEntityId PersistentOf(const World& components, EntityId entity)
{
    const auto* id = components.TryGet<PersistentIdComponent>(entity);
    return id != nullptr ? id->Id : PersistentEntityId{};
}
} // namespace

LightingDiagnostics::LightingDiagnostics(EditorWorkspace& workspace, CommandStack& commands, LightingReports reports)
    : Workspace(workspace)
    , Commands(commands)
    , Reports(std::move(reports))
{
}

void LightingDiagnostics::Offer(ActionCatalog& catalog, ActionContext& context)
{
    if (Reports.Preview && Reports.ShowPreview)
        OfferAction(catalog, context, { .Id = "view.baked_lighting", .Label = "Baked Lighting", .Category = "View" },
                    { .Invoke = [this] { Reports.ShowPreview(Reports.Preview() == BakedLightingPreview::Off); },
                      .Enabled = [this] { return Reports.Preview() != BakedLightingPreview::Unavailable; },
                      .Checked = [this] {
                          const BakedLightingPreview state = Reports.Preview();
                          return state == BakedLightingPreview::Fresh || state == BakedLightingPreview::Stale;
                      } });
    if (Reports.RerenderShadows)
        OfferAction(catalog, context, { .Id = "view.rerender_shadows", .Label = "Re-render Shadows", .Category = "View" },
                    { .Invoke = [this] { Reports.RerenderShadows(); } });
}

std::vector<Problem> LightingDiagnostics::Problems(const DocumentRef& document) const
{
    std::vector<Problem> problems;
    if (const ShadowResidencyReadout* readout = Reports.Shadows; readout != nullptr && readout->Active)
    {
        const EditorDocument& focus = Workspace.World.FocusDocument();
        for (const ShadowResidencyReadout::LightRow& row : readout->Rows)
        {
            if (row.Held)
                continue;
            const bool point = row.Type == GpuLightType::Point;
            const PersistentEntityId id = PersistentOf(focus.GetRegistry().Components, row.Entity);
            problems.push_back({
                .Severity = ProblemSeverity::Warning,
                .Message = std::format("{} light {} casts no shadow: all {} {} shadow slots are taken ({})",
                                       TypeLabel(row.Type), EntityLabel(focus, row.Entity),
                                       point ? readout->Budgets.MaxPointSlots : readout->Budgets.MaxSlots,
                                       point ? "point" : "spot",
                                       point ? "render.shadow.max_point" : "render.shadow.max_spot"),
                .Location = id.IsValid() ? LevelLocationKey(id) : std::string{},
                .Document = document,
            });
        }
    }
    if (Reports.Preview && Reports.Preview() == BakedLightingPreview::Stale)
        problems.push_back({ .Severity = ProblemSeverity::Info,
                             .Message = "The baked lighting preview shows an older cook; cook again to refresh it",
                             .Document = document });
    return problems;
}

bool LightingDiagnostics::Reveal(std::string_view location)
{
    const std::optional<LevelLocation> parsed = ParseLevelLocation(location);
    const PersistentEntityId* id = parsed.has_value() ? std::get_if<PersistentEntityId>(&*parsed) : nullptr;
    if (id == nullptr)
        return false;
    const Registry& registry = Workspace.World.FocusDocument().GetRegistry();
    const EntityId entity = Workspace.World.FocusDocument().GetScene().EntityWithPersistentId(*id);
    if (!entity.IsValid())
        return false;
    Commands.Execute(std::make_unique<SelectCommand>(Workspace.Selection,
                                                     SelectableRef::EntitySelection(registry.Id, entity)));
    return true;
}

EntityId LightingDiagnostics::SelectedEntity() const
{
    const SelectableRef selected = Workspace.Selection.GetPrimarySelection();
    if (!selected.IsEntity() || selected.Registry != Workspace.World.FocusDocument().GetRegistry().Id)
        return {};
    return selected.Entity;
}

std::vector<PropertyNode> LightingDiagnostics::Nodes() const
{
    const EntityId entity = SelectedEntity();
    if (!entity.IsValid())
        return {};
    std::vector<PropertyNode> nodes = ShadowNodes(entity);
    const World& components = Workspace.World.FocusDocument().GetRegistry().Components;
    if (LightingReadModel::BakesDirect(components, entity))
        nodes.push_back(NoteProperty("baked", "Direct light",
                                     "Baked into the zone's lightmap at cook; not a runtime light"));
    if (LightingReadModel::IsIrradianceVolume(components, entity))
    {
        const std::optional<LightingCook> cook = Reports.LastCook ? Reports.LastCook() : std::nullopt;
        nodes.push_back(NoteProperty("probes", "Last cook",
                                     cook.has_value()
                                         ? std::format("{} volumes, {} probes baked", cook->ProbeVolumes, cook->Probes)
                                         : std::string("Not cooked yet")));
    }
    return nodes;
}

std::vector<PropertyNode> LightingDiagnostics::ShadowNodes(EntityId entity) const
{
    const ShadowResidencyReadout* readout = Reports.Shadows;
    if (readout == nullptr || !readout->Active || readout->FocusRegistry != Workspace.World.FocusDocument().GetRegistry().Id)
        return {};
    const auto row = std::ranges::find(readout->Rows, entity, &ShadowResidencyReadout::LightRow::Entity);
    if (row == readout->Rows.end())
        return {};

    const ShadowFrameStats& stats = readout->Stats;
    const ShadowResidencyBudgets& budgets = readout->Budgets;
    std::vector<PropertyNode> nodes;
    if (row->Type == GpuLightType::Point)
    {
        const double kilobytes = kPointShadowFaceExtent * kPointShadowFaceExtent * 2.0 * kPointShadowFaceCount / 1024.0;
        nodes.push_back(NoteProperty("tier", "Tier", std::format("{} cube", kPointShadowFaceExtent)));
        nodes.push_back(NoteProperty("cost", "Cost", std::format("6 views per update, {:.0f} KB", kilobytes)));
        nodes.push_back(NoteProperty("pool", "Pool", std::format("{} of {} cubes held", stats.Point.HeldRequests,
                                                                 budgets.MaxPointSlots)));
    }
    else
    {
        // A held tile the arbiter shrank to fit the atlas reads as the tier asked for and the one granted.
        const std::uint32_t granted = row->Held ? readout->Slots[row->Slot].Allocation.Size : row->TileSize;
        const double kilobytes = static_cast<double>(granted) * granted * 2.0 / 1024.0;
        nodes.push_back(NoteProperty("tier", "Tier", granted == row->TileSize
                                                         ? TierLabel(granted)
                                                         : std::format("{} -> {}", row->TileSize, TierLabel(granted))));
        nodes.push_back(NoteProperty("cost", "Cost", std::format("1 view per update, {:.0f} KB", kilobytes)));
        nodes.push_back(NoteProperty("pool", "Pool",
                                     std::format("{} of {} spot slots held, atlas {:.0f}% full", stats.Spot.HeldRequests,
                                                 budgets.MaxSlots, AtlasOccupancy(*readout))));
    }
    nodes.push_back(NoteProperty("update", "Update", std::string(PolicyLabel(row->Policy))));
    nodes.push_back(NoteProperty("state", "State", StateOf(*readout, *row)));
    nodes.push_back(NoteProperty(
        "views", "This frame",
        budgets.MaxViewsPerFrame == 0
            ? std::format("{} views rendered, {} cached", stats.ViewsScheduled,
                          stats.Spot.CachedSlots + stats.Point.CachedSlots)
            : std::format("{} of {} views rendered, {} cached", stats.ViewsScheduled, budgets.MaxViewsPerFrame,
                          stats.Spot.CachedSlots + stats.Point.CachedSlots)));
    PropertyNode shadow = GroupProperty("shadow", "Shadow");
    shadow.Children = std::move(nodes);
    return { std::move(shadow) };
}
