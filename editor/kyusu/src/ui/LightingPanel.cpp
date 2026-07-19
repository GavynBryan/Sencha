#include "LightingPanel.h"

#include "render/ShadowResidencyReadout.h"

#include "commands/CommandStack.h"
#include "selection/SelectionService.h"
#include "selection/commands/SelectCommand.h"
#include "ui/EditorUiStyle.h"
#include "ui/ScopedPanel.h"

#include <imgui.h>

#include <algorithm>
#include <bit>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>

namespace
{
    const char* PolicyLabel(ShadowUpdatePolicy policy)
    {
        switch (policy)
        {
        case ShadowUpdatePolicy::EveryFrame: return "Every frame";
        case ShadowUpdatePolicy::OnChange:   return "On change";
        case ShadowUpdatePolicy::Static:     return "Static";
        }
        return "?";
    }

    const char* LightTypeLabel(GpuLightType type)
    {
        return type == GpuLightType::Point ? "Point" : "Spot";
    }

    // "512 (496 usable)": physical tile edge with the guard-band interior the
    // filter actually samples.
    std::string TierLabel(std::uint32_t tileSize)
    {
        char buffer[48];
        std::snprintf(buffer, sizeof(buffer), "%u (%u usable)", tileSize,
                      tileSize - 2u * kSpotShadowGuardTexels);
        return buffer;
    }

    ImVec4 TierColor(std::uint32_t tileSize)
    {
        if (tileSize >= 1024)
            return EditorUi::Accent;
        if (tileSize >= 512)
            return EditorUi::Secondary;
        return EditorUi::Warning;
    }

    ImVec4 WithAlpha(ImVec4 color, float alpha)
    {
        color.w = alpha;
        return color;
    }
}

LightingPanel::LightingPanel(const ShadowResidencyReadout& readout,
                             SelectionService& selection,
                             CommandStack& commands,
                             std::function<void()> invalidateShadows,
                             std::function<std::uint32_t()> countBakedDirectLights,
                             std::function<BakedPreviewState()> bakedPreviewState,
                             std::function<void(bool)> setBakedPreview)
    : Readout(readout)
    , Selection(selection)
    , Commands(commands)
    , InvalidateShadows(std::move(invalidateShadows))
    , CountBakedDirectLights(std::move(countBakedDirectLights))
    , BakedPreview(std::move(bakedPreviewState))
    , SetBakedPreview(std::move(setBakedPreview))
{
}

std::string_view LightingPanel::GetTitle() const
{
    return "Lighting";
}

void LightingPanel::OnDraw()
{
    ScopedPanel panel(GetTitle(), &Visible);
    if (!panel.IsOpen())
        return;

    // Baked lights never appear in the shadow readout (they hold no runtime
    // slot), so their count is surfaced here to confirm the authoring took.
    if (CountBakedDirectLights)
    {
        const std::uint32_t baked = CountBakedDirectLights();
        if (baked > 0)
        {
            ImGui::Text("Baked direct lights: %u", baked);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Lights authored bake_contribution=direct: their diffuse\n"
                                  "bakes into the zone's lightmap atlas at cook and they are\n"
                                  "excluded from the runtime light set.");
        }
    }

    // Viewport baked-lighting preview: renders the LAST COOK's cells + atlas
    // in the Solid viewports. A cook is the refresh action.
    if (BakedPreview && SetBakedPreview)
    {
        const BakedPreviewState state = BakedPreview();
        if (state == BakedPreviewState::Unavailable)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, EditorUi::TextDim);
            ImGui::TextUnformatted("Baked preview: cook to enable");
            ImGui::PopStyleColor();
        }
        else
        {
            bool enabled = state != BakedPreviewState::Off;
            if (ImGui::Checkbox("Baked lighting preview", &enabled))
                SetBakedPreview(enabled);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Show the last cook's baked lighting in Solid viewports.\n"
                                  "Cook again to refresh after edits.");
            if (state == BakedPreviewState::Stale)
            {
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, EditorUi::Warning);
                ImGui::TextUnformatted("(stale - recook to refresh)");
                ImGui::PopStyleColor();
            }
        }
    }

    if (!Readout.Active)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, EditorUi::TextDim);
        ImGui::TextWrapped("Shadow preview inactive (no shadow targets). Lights render unshadowed.");
        ImGui::PopStyleColor();
        return;
    }

    DrawBudgetHeader();
    DrawDeniedWarning();
    ImGui::Separator();
    DrawLightRows();
    DrawSelectedCostLine();
    ImGui::Separator();
    DrawAtlasMap();
}

void LightingPanel::DrawBudgetHeader()
{
    const ShadowFrameStats& stats = Readout.Stats;
    ImGui::Text("Spot shadows: %u requested, %u resident",
                stats.Spot.RequestCount, stats.Spot.HeldRequests);
    ImGui::Text("Point shadows: %u requested, %u resident",
                stats.Point.RequestCount, stats.Point.HeldRequests);
    ImGui::Text("Slots spot %u / %u, point %u / %u",
                stats.Spot.HeldRequests, Readout.Budgets.MaxSlots,
                stats.Point.HeldRequests, Readout.Budgets.MaxPointSlots);
    ImGui::SameLine();
    if (Readout.Budgets.MaxViewsPerFrame == 0)
        ImGui::Text("| views %u (unclamped)", stats.ViewsScheduled);
    else
        ImGui::Text("| views %u / %u", stats.ViewsScheduled,
                    Readout.Budgets.MaxViewsPerFrame);
    ImGui::SameLine();
    ImGui::Text("| cached %u", stats.Spot.CachedSlots + stats.Point.CachedSlots);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Resident lights whose shadow needed no re-render this frame.\n"
                          "A still scene of On change / Static lights should render 0 views.\n"
                          "Budgets: render.shadow.max_spot / max_point / max_views_per_frame cvars.");

    if (ImGui::SmallButton("Re-render all"))
    {
        if (InvalidateShadows)
            InvalidateShadows();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Invalidate every cached shadow (render.shadow.invalidate).");
}

void LightingPanel::DrawDeniedWarning()
{
    const std::uint32_t denied = Readout.Stats.Spot.DeniedRequests
                               + Readout.Stats.Point.DeniedRequests;
    if (denied == 0)
        return;

    ImGui::PushStyleColor(ImGuiCol_Text, EditorUi::Warning);
    ImGui::TextWrapped("Over budget: %u light(s) render unshadowed:", denied);
    ImGui::PopStyleColor();
    for (const ShadowResidencyReadout::LightRow& row : Readout.Rows)
    {
        if (row.Held)
            continue;
        ImGui::SameLine();
        char label[32];
        std::snprintf(label, sizeof(label), "%s %u##denied%u_%u",
                      LightTypeLabel(row.Type), row.Entity.Index,
                      row.Entity.Index, static_cast<unsigned>(row.Type));
        if (ImGui::SmallButton(label))
            SelectLight(row.Entity);
    }
}

void LightingPanel::DrawLightRows()
{
    if (Readout.Rows.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, EditorUi::TextDim);
        ImGui::TextUnformatted("No shadow-casting lights in the focus scene.");
        ImGui::PopStyleColor();
        return;
    }

    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("shadow_rows", 5, flags))
        return;
    ImGui::TableSetupColumn("Light", ImGuiTableColumnFlags_None, 1.2f);
    ImGui::TableSetupColumn("Tier", ImGuiTableColumnFlags_None, 1.4f);
    ImGui::TableSetupColumn("Update", ImGuiTableColumnFlags_None, 1.1f);
    ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_None, 0.6f);
    ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_None, 1.2f);
    ImGui::TableHeadersRow();

    const SelectableRef selected = Selection.GetPrimarySelection();
    for (const ShadowResidencyReadout::LightRow& row : Readout.Rows)
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();

        char label[32];
        std::snprintf(label, sizeof(label), "%s %u##row%u_%u",
                      LightTypeLabel(row.Type), row.Entity.Index,
                      row.Entity.Index, static_cast<unsigned>(row.Type));
        const bool isSelected = selected.IsEntity()
            && selected.Registry == Readout.FocusRegistry
            && selected.Entity == row.Entity;
        if (ImGui::Selectable(label, isSelected,
                              ImGuiSelectableFlags_SpanAllColumns))
            SelectLight(row.Entity);

        ImGui::TableNextColumn();
        if (row.Type == GpuLightType::Point)
        {
            ImGui::Text("%u cube", kPointShadowFaceExtent);
        }
        else if (row.Held && Readout.Slots[row.Slot].Allocation.Size != row.TileSize)
        {
            // The arbiter downgraded the tier to fit the atlas.
            ImGui::PushStyleColor(ImGuiCol_Text, EditorUi::Warning);
            ImGui::Text("%u -> %s", row.TileSize,
                        TierLabel(Readout.Slots[row.Slot].Allocation.Size).c_str());
            ImGui::PopStyleColor();
        }
        else
        {
            ImGui::TextUnformatted(TierLabel(row.TileSize).c_str());
        }

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(PolicyLabel(row.Policy));

        ImGui::TableNextColumn();
        if (row.Held)
            ImGui::Text("%u", row.Slot);
        else
            ImGui::TextUnformatted("-");

        ImGui::TableNextColumn();
        if (!row.Held)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, EditorUi::Warning);
            ImGui::TextUnformatted("denied");
            ImGui::PopStyleColor();
        }
        else
        {
            if (row.Type == GpuLightType::Point)
            {
                const PointShadowSlotInfo& slot = Readout.PointSlots[row.Slot];
                if (!slot.EverRendered)
                    ImGui::Text("pending %u/6", 6u - std::popcount(slot.PendingFaces));
                else if (slot.Invalid || slot.PendingFaces != 0)
                    ImGui::Text("queued %u/6", 6u - std::popcount(slot.PendingFaces));
                else if (slot.FramesSinceRendered == 0)
                    ImGui::TextUnformatted("rendered");
                else
                    ImGui::Text("cached %uf", slot.FramesSinceRendered);
            }
            else
            {
                const SpotShadowSlotInfo& slot = Readout.Slots[row.Slot];
                if (!slot.EverRendered)
                    ImGui::TextUnformatted("pending");
                else if (slot.Invalid)
                    ImGui::TextUnformatted("queued");
                else if (slot.FramesSinceRendered == 0)
                    ImGui::TextUnformatted("rendered");
                else
                    ImGui::Text("cached %uf", slot.FramesSinceRendered);
            }
        }
    }
    ImGui::EndTable();
}

void LightingPanel::DrawSelectedCostLine()
{
    const SelectableRef selected = Selection.GetPrimarySelection();
    if (!selected.IsEntity() || !(selected.Registry == Readout.FocusRegistry))
        return;
    for (const ShadowResidencyReadout::LightRow& row : Readout.Rows)
    {
        if (!(row.Entity == selected.Entity))
            continue;
        if (row.Type == GpuLightType::Point)
        {
            const float kilobytes = static_cast<float>(kPointShadowFaceExtent)
                                  * static_cast<float>(kPointShadowFaceExtent)
                                  * 2.0f * kPointShadowFaceCount / 1024.0f;
            ImGui::PushStyleColor(ImGuiCol_Text, EditorUi::TextDim);
            ImGui::Text("Selected: Point, %u: 6 views per update, %.0f KB",
                        kPointShadowFaceExtent, kilobytes);
            ImGui::PopStyleColor();
            return;
        }
        const std::uint32_t size = row.Held
            ? Readout.Slots[row.Slot].Allocation.Size
            : row.TileSize;
        // D16 tiles: two bytes per texel.
        const float kilobytes = static_cast<float>(size) * static_cast<float>(size)
                              * 2.0f / 1024.0f;
        ImGui::PushStyleColor(ImGuiCol_Text, EditorUi::TextDim);
        ImGui::Text("Selected: Spot, %u: 1 view per update, %.0f KB", size, kilobytes);
        ImGui::PopStyleColor();
        return;
    }
}

void LightingPanel::DrawAtlasMap()
{
    if (!ImGui::CollapsingHeader("Shadow pools", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    // Occupancy by physical tier.
    std::uint32_t tierCounts[3] = {};
    std::uint64_t usedArea = 0;
    for (const SpotShadowSlotInfo& slot : Readout.Slots)
    {
        if (!slot.Live || !slot.Allocation.IsValid())
            continue;
        usedArea += static_cast<std::uint64_t>(slot.Allocation.Size)
                  * slot.Allocation.Size;
        if (slot.Allocation.Size >= 1024)
            ++tierCounts[0];
        else if (slot.Allocation.Size >= 512)
            ++tierCounts[1];
        else
            ++tierCounts[2];
    }
    const float occupancy = 100.0f * static_cast<float>(usedArea)
        / (static_cast<float>(kSpotShadowAtlasExtent) * kSpotShadowAtlasExtent);
    ImGui::Text("%ux%u D16, %.0f%% occupied (1024: %u, 512: %u, 256: %u)",
                kSpotShadowAtlasExtent, kSpotShadowAtlasExtent, occupancy,
                tierCounts[0], tierCounts[1], tierCounts[2]);
    ImGui::Text("Point cube pool: %u / %u cubes, %u faces each",
                Readout.Stats.Point.HeldRequests, Readout.Budgets.MaxPointSlots,
                kPointShadowFaceCount);

    const float side = std::min(ImGui::GetContentRegionAvail().x, 240.0f);
    if (side < 64.0f)
        return;
    const float scale = side / static_cast<float>(kSpotShadowAtlasExtent);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    draw->AddRectFilled(origin, ImVec2(origin.x + side, origin.y + side),
                        ImGui::GetColorU32(EditorUi::FrameBg));
    draw->AddRect(origin, ImVec2(origin.x + side, origin.y + side),
                  ImGui::GetColorU32(EditorUi::Border));

    for (std::uint32_t index = 0; index < kMaxSpotShadows; ++index)
    {
        const SpotShadowSlotInfo& slot = Readout.Slots[index];
        if (!slot.Live || !slot.Allocation.IsValid())
            continue;

        const ImVec2 tileMin(origin.x + slot.Allocation.X * scale,
                             origin.y + slot.Allocation.Y * scale);
        const ImVec2 tileMax(tileMin.x + slot.Allocation.Size * scale,
                             tileMin.y + slot.Allocation.Size * scale);
        const ImVec4 tier = TierColor(slot.Allocation.Size);
        draw->AddRectFilled(tileMin, tileMax,
                            ImGui::GetColorU32(WithAlpha(tier, slot.Invalid ? 0.15f : 0.35f)));
        draw->AddRect(tileMin, tileMax, ImGui::GetColorU32(WithAlpha(tier, 0.9f)));

        // The guard-band inset: the interior the filter actually samples.
        const float guard = kSpotShadowGuardTexels * scale;
        draw->AddRect(ImVec2(tileMin.x + guard, tileMin.y + guard),
                      ImVec2(tileMax.x - guard, tileMax.y - guard),
                      ImGui::GetColorU32(WithAlpha(EditorUi::TextDim, 0.6f)));

        char label[24];
        if (slot.EverRendered)
            std::snprintf(label, sizeof(label), "%u:%uf", index,
                          slot.FramesSinceRendered);
        else
            std::snprintf(label, sizeof(label), "%u:-", index);
        draw->AddText(ImVec2(tileMin.x + guard + 2.0f, tileMin.y + guard + 1.0f),
                      ImGui::GetColorU32(EditorUi::TextPrimary), label);

        if (ImGui::IsMouseHoveringRect(tileMin, tileMax))
        {
            ImGui::SetTooltip("Slot %u: Spot %u\n%u tile, %s\nheld %u frames, %s",
                              index, slot.Owner.Entity.Index, slot.Allocation.Size,
                              PolicyLabel(slot.Policy), slot.FramesSinceAcquired,
                              !slot.EverRendered ? "awaiting first render"
                              : slot.Invalid     ? "re-render queued"
                                                 : "cached");
        }
    }
    ImGui::Dummy(ImVec2(side, side));
}

void LightingPanel::SelectLight(EntityId entity)
{
    Commands.Execute(std::make_unique<SelectCommand>(
        Selection, SelectableRef::EntitySelection(Readout.FocusRegistry, entity)));
}
