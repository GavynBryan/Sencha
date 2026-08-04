#include "MovementResponsePanel.h"

#include "MovementLayerSummary.h"
#include "MovementProfileForm.h"
#include "MovementResolvePreview.h"
#include "MovementResponseSim.h"

#include "../DataDocument.h"
#include "../DataEditorWorkspace.h"

#include "ui/EditorUiStyle.h"
#include "ui/ScopedPanel.h"

#include <imgui.h>

#include <algorithm>
#include <format>
#include <string>

namespace
{
    void Metric(const char* label, const std::string& value, const char* help)
    {
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", label);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(value.c_str());
        if (help != nullptr && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", help);
    }
}

MovementResponsePanel::MovementResponsePanel(DataEditorWorkspace& workspace,
                                             MovementResolvePreview& preview)
    : Workspace(workspace)
    , Preview(preview)
{
}

void MovementResponsePanel::OnDraw()
{
    ScopedPanel panel(GetTitle(), &Visible);
    if (!panel.IsOpen())
        return;

    const DataDocument* document = Workspace.Active();
    if (document == nullptr || document->Subtype() != MovementProfileSubtype())
    {
        ImGui::TextDisabled("Open a movement profile to see how it feels.");
        return;
    }
    if (!Preview.IsBound())
    {
        ImGui::TextColored(EditorUi::Warning, "%s",
                           Preview.Error().empty() ? "The profile does not compile."
                                                   : Preview.Error().c_str());
        return;
    }

    MovementSimParams params;
    params.MoveSpeed = Preview.Context().MoveSpeed;

    const MovementSimContextFn makeContext =
        [this](SupportKind support, std::span<const std::string> tags,
               GameplayTagContainer& storage)
        {
            const std::vector<std::string> names(tags.begin(), tags.end());
            return Preview.MakeResolveContext(support, 0.0f, names, storage);
        };

    const MovementJumpArc arc = SimulateJumpArc(*Preview.BoundProfile(), makeContext, params);
    const MovementSpeedResponse response =
        SimulateSpeedResponse(*Preview.BoundProfile(), makeContext, params);

    if (ImGui::BeginTable("metrics", 4, ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableNextRow();
        Metric("Jump height", std::format("{} m", FormatMovementCoefficient(arc.ApexHeight)),
               "Peak height above the launch point, at the resolved jump speed and gravity.");
        Metric("Time to apex", std::format("{} s", FormatMovementCoefficient(arc.TimeToApex)),
               "How long the rise takes. Short and floaty are set here.");

        ImGui::TableNextRow();
        Metric("Hang time", std::format("{} s", FormatMovementCoefficient(arc.HangTime)),
               arc.Landed ? "Launch until the character is back at launch height."
                          : "The character had not landed within the simulated window.");
        Metric("Hop distance", std::format("{} m", FormatMovementCoefficient(arc.Distance)),
               "Ground covered over one hop with the stick held forward.");

        ImGui::TableNextRow();
        Metric("Top speed", std::format("{} m/s", FormatMovementCoefficient(response.TopSpeed)),
               "Where ground acceleration and friction settle.");
        Metric("Time to top speed",
               response.ReachedTopSpeed
                   ? std::format("{} s", FormatMovementCoefficient(response.TimeToTopSpeed))
                   : std::string("n/a"),
               "From a standing start to within 5% of top speed.");

        ImGui::TableNextRow();
        Metric("Stopping distance",
               std::format("{} m", FormatMovementCoefficient(response.StoppingDistance)),
               "How far the character slides after the stick is released.");
        Metric("Jump speed", std::format("{} m/s", FormatMovementCoefficient(arc.JumpSpeed)),
               "The resolved launch speed the jump applies.");
        ImGui::EndTable();
    }

    if (!arc.Landed)
    {
        ImGui::TextColored(EditorUi::Warning,
                           "The character never came back down within the simulated window.");
    }

    const float plotHeight = std::max(60.0f, ImGui::GetContentRegionAvail().y * 0.45f);

    HeightPlot.clear();
    HeightPlot.reserve(arc.Samples.size());
    for (const MovementSimSample& sample : arc.Samples)
        HeightPlot.push_back(sample.Height);
    if (!HeightPlot.empty())
    {
        const std::string overlay = std::format("jump arc  ({} s)",
                                                FormatMovementCoefficient(arc.HangTime));
        ImGui::PlotLines("##jump", HeightPlot.data(), static_cast<int>(HeightPlot.size()),
                         0, overlay.c_str(), 0.0f, FLT_MAX, ImVec2(-1.0f, plotHeight));
    }

    SpeedPlot.clear();
    SpeedPlot.reserve(response.Samples.size());
    for (const MovementSimSample& sample : response.Samples)
        SpeedPlot.push_back(sample.PlanarSpeed);
    if (!SpeedPlot.empty())
    {
        const std::string overlay = std::format("speed up then stop  (top {} m/s)",
                                                FormatMovementCoefficient(response.TopSpeed));
        ImGui::PlotLines("##speed", SpeedPlot.data(), static_cast<int>(SpeedPlot.size()),
                         0, overlay.c_str(), 0.0f, FLT_MAX, ImVec2(-1.0f, plotHeight));
    }
}
