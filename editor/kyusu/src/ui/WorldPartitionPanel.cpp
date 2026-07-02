#include "WorldPartitionPanel.h"

#include "ui/EditorUiStyle.h"
#include "ui/ScopedPanel.h"
#include "fonts/IconsFontAwesome6.h"

#include "document/WorldDocument.h"

#include <imgui.h>

#include <cstring>
#include <string>

WorldPartitionPanel::WorldPartitionPanel(WorldDocument& world)
    : WorldDoc(world)
{
}

std::string_view WorldPartitionPanel::GetTitle() const
{
    return "World";
}

void WorldPartitionPanel::OnDraw()
{
    if (!WorldDoc.IsWorld())
        return;

    ScopedPanel panel(GetTitle(), &Visible);
    if (!panel.IsOpen())
        return;

    if (ImGui::Button(ICON_FA_PLUS "  New Region"))
        (void)WorldDoc.AddRegion("New Region");
    ImGui::Separator();

    for (const RegionRecord& region : WorldDoc.Manifest().Regions)
        DrawRegion(region);

    // Zones whose region reference resolves to no region record still need a
    // home in the tree, or they would be unreachable for repair.
    bool orphanHeader = false;
    for (const ZoneHeader& zone : WorldDoc.Manifest().Zones)
    {
        const bool known = [&]
        {
            for (const RegionRecord& region : WorldDoc.Manifest().Regions)
                if (region.Id == zone.Region)
                    return true;
            return false;
        }();
        if (known)
            continue;
        if (!orphanHeader)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, EditorUi::TextDim);
            ImGui::TextUnformatted("Unassigned");
            ImGui::PopStyleColor();
            orphanHeader = true;
        }
        DrawZoneRow(zone);
    }

    ImGui::Separator();
    DrawValidation();
}

bool WorldPartitionPanel::DrawRenameField(bool active)
{
    if (!active)
        return false;
    ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##rename", RenameBuffer_, sizeof(RenameBuffer_));
    return true;
}

void WorldPartitionPanel::DrawRegion(const RegionRecord& region)
{
    ImGui::PushID(static_cast<int>(region.Id.Value & 0x7fffffff));

    if (RenamingRegion_ == region.Id)
    {
        DrawRenameField(true);
        if (ImGui::IsItemDeactivated())
        {
            if (RenameBuffer_[0] != '\0')
                (void)WorldDoc.RenameRegion(region.Id, RenameBuffer_);
            RenamingRegion_ = RegionId{};
        }
        ImGui::PopID();
        return;
    }

    if (NavigateRegion_ == region.Id)
    {
        ImGui::SetNextItemOpen(true);
        NavigateRegion_ = RegionId{};
    }
    const bool open = ImGui::TreeNodeEx(region.Name.c_str(),
                                        ImGuiTreeNodeFlags_DefaultOpen
                                            | ImGuiTreeNodeFlags_SpanAvailWidth);

    if (ImGui::BeginPopupContextItem("##region_ctx"))
    {
        if (ImGui::MenuItem(ICON_FA_PLUS "  New Zone"))
            (void)WorldDoc.AddZone(region.Id, "New Zone");
        if (ImGui::MenuItem(ICON_FA_PEN "  Rename"))
        {
            RenamingRegion_ = region.Id;
            std::strncpy(RenameBuffer_, region.Name.c_str(), sizeof(RenameBuffer_) - 1);
            RenameBuffer_[sizeof(RenameBuffer_) - 1] = '\0';
        }
        ImGui::EndPopup();
    }

    if (open)
    {
        for (const ZoneHeader& zone : WorldDoc.Manifest().Zones)
        {
            if (zone.Region == region.Id)
                DrawZoneRow(zone);
        }
        ImGui::TreePop();
    }

    ImGui::PopID();
}

void WorldPartitionPanel::DrawZoneRow(const ZoneHeader& zone)
{
    ImGui::PushID(static_cast<int>(zone.Id.Value & 0x7fffffff));

    const bool isFocus = WorldDoc.FocusZone() == zone.Id;
    const bool isOpen = WorldDoc.IsZoneOpen(zone.Id);

    bool visible = true;
    bool dirty = false;
    if (isOpen)
    {
        WorldDoc.VisitOpenZones([&](ZoneId id, EditorDocument& document, const ZoneViewState& view)
                                {
                                    if (id != zone.Id)
                                        return;
                                    visible = view.VisibleInEditor;
                                    dirty = document.IsDirty();
                                });
    }

    if (RenamingZone_ == zone.Id)
    {
        DrawRenameField(true);
        if (ImGui::IsItemDeactivated())
        {
            if (RenameBuffer_[0] != '\0')
                (void)WorldDoc.RenameZone(zone.Id, RenameBuffer_);
            RenamingZone_ = ZoneId{};
        }
        ImGui::PopID();
        return;
    }

    if (isOpen)
    {
        if (ImGui::SmallButton(visible ? ICON_FA_EYE : ICON_FA_EYE_SLASH))
            (void)WorldDoc.SetZoneVisible(zone.Id, !visible);
        ImGui::SameLine();
    }

    // State badge: F focus, C context (open), H hidden; header-only rows get none.
    if (isFocus)
        ImGui::TextColored(EditorUi::Accent, "F");
    else if (isOpen && !visible)
        ImGui::TextColored(EditorUi::TextDim, "H");
    else if (isOpen)
        ImGui::TextColored(EditorUi::TextPrimary, "C");
    if (isFocus || isOpen)
        ImGui::SameLine();

    const bool headerOnly = !isOpen;
    if (headerOnly)
        ImGui::PushStyleColor(ImGuiCol_Text, EditorUi::TextDim);

    std::string label = zone.Name;
    if (dirty)
        label += " " ICON_FA_CIRCLE_DOT;
    label += "##zone_row";
    const bool selected = isFocus || SelectedZoneRow_ == zone.Id;
    ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick);
    if (headerOnly)
        ImGui::PopStyleColor();

    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        (void)WorldDoc.SetFocusZone(zone.Id);

    if (ImGui::BeginPopupContextItem("##zone_ctx"))
    {
        if (ImGui::MenuItem("Focus"))
            (void)WorldDoc.SetFocusZone(zone.Id);
        if (ImGui::MenuItem("Load As Context", nullptr, false, !isOpen))
            (void)WorldDoc.LoadZone(zone.Id);
        const bool canUnload = isOpen && !dirty && !isFocus;
        if (ImGui::MenuItem("Unload", nullptr, false, canUnload))
            (void)WorldDoc.UnloadZone(zone.Id);
        if (isOpen && dirty && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Save the zone first");
        if (ImGui::MenuItem(ICON_FA_PEN "  Rename"))
        {
            RenamingZone_ = zone.Id;
            std::strncpy(RenameBuffer_, zone.Name.c_str(), sizeof(RenameBuffer_) - 1);
            RenameBuffer_[sizeof(RenameBuffer_) - 1] = '\0';
        }
        ImGui::EndPopup();
    }

    ImGui::PopID();
}

void WorldPartitionPanel::DrawValidation()
{
    const auto records = WorldDoc.ValidationRecords();
    if (records.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, EditorUi::TextDim);
        ImGui::TextUnformatted("Validation: clean");
        ImGui::PopStyleColor();
        return;
    }

    ImGui::TextUnformatted("Validation");
    int rowId = 0;
    for (const ContentRiskRecord& record : records)
    {
        ImGui::PushID(rowId++);
        const bool isError = record.Severity == ContentRiskSeverity::Error;
        ImGui::TextColored(isError ? EditorUi::Danger : EditorUi::Warning,
                           isError ? ICON_FA_CIRCLE_XMARK : ICON_FA_TRIANGLE_EXCLAMATION);
        ImGui::SameLine();
        const std::string label = record.RuleId + "##validation_row";
        if (ImGui::Selectable(label.c_str()))
            NavigateToRecord(record);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", record.Message.c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, EditorUi::TextDim);
        ImGui::TextWrapped("%s", record.Message.c_str());
        ImGui::PopStyleColor();
        ImGui::PopID();
    }
}

void WorldPartitionPanel::NavigateToRecord(const ContentRiskRecord& record)
{
    if (record.Kind == ContentRiskSourceKind::Region)
    {
        NavigateRegion_ = RegionId{ record.SourceId };
        return;
    }
    if (record.Kind != ContentRiskSourceKind::Zone)
        return;

    const ZoneId zone{ record.SourceId };
    SelectedZoneRow_ = zone;
    for (const ZoneHeader& header : WorldDoc.Manifest().Zones)
    {
        if (header.Id == zone)
        {
            NavigateRegion_ = header.Region;
            return;
        }
    }
}
