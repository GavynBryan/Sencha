#include "ui/AnimationEventPanels.h"

#include "authoring/AnimationPreviewWorkspace.h"
#include "ui/WorkspaceView.h"
#include "ui/IEditorPanel.h"
#include "ui/ScopedPanel.h"

#include <assets/runtime/RuntimeAssets.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <imgui.h>

#include <algorithm>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{
class EventAdmissionsPanel final : public IEditorPanel
{
public:
    explicit EventAdmissionsPanel(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}
    std::string_view GetTitle() const override { return "Event admissions"; }
    PanelPersistence GetPersistence() const override { return { "animation.event_admissions" }; }
    DockSlot GetDockSlot() const override { return DockSlot::RightBottom; }

    void OnDraw() override
    {
        if (!IsVisible())
            return;
        ScopedPanel panel(GetWindowName(), &Visible);
        if (!panel.IsOpen())
            return;
        AnimationPreviewSession& session = Workspace.Rig.Simulation;
        if (!session.IsOpen())
        {
            ImGui::TextDisabled("Open a rig to simulate it.");
            return;
        }
        DrawDispatch(session);
        ImGui::Separator();
        DrawHistory(session);
    }

private:
    void DrawDispatch(AnimationPreviewSession& session)
    {
        int role = session.Scenario().Role == AnimationPreviewRole::Client ? 1 : 0;
        ImGui::TextUnformatted("Preview role:");
        ImGui::SameLine();
        if (ImGui::RadioButton("Authority", &role, 0))
            session.SetRole(AnimationPreviewRole::Authority);
        ImGui::SameLine();
        if (ImGui::RadioButton("Client", &role, 1))
            session.SetRole(AnimationPreviewRole::Client);

        if (ImGui::TreeNode("Recorders"))
        {
            ImGui::TextWrapped("Nothing runs the game here. A verb without a recorder answers Unavailable; a "
                               "recorder accepts and keeps what it was handed. Its results are labelled as a "
                               "recorder's, never as the game's.");
            if (const VerbRegistry* verbs = session.Verbs())
                for (const VerbId id : verbs->Live())
                    if (const VerbDefinition* definition = verbs->Get(id))
                    {
                        bool attached = session.HasRecorder(definition->Name);
                        if (ImGui::Checkbox(definition->Name.c_str(), &attached))
                            session.SetRecorder(definition->Name, attached);
                    }
            ImGui::TreePop();
        }
    }

    std::string EventLabel(const AnimationPreviewSession& session, const AnimDecisionRecord& record) const
    {
        const AnimBoundRig* rig = session.Rig();
        if (rig == nullptr || record.Content >= rig->Contents.size())
            return std::format("{}", record.EventKey);
        const AnimationClipData* clip = Workspace.Clips().Get(rig->Contents[record.Content].Clip);
        if (clip != nullptr)
            for (const AnimationClipEvent& event : clip->Events)
                if (event.Key == record.EventKey)
                    return event.Name.empty() ? std::format("{} ({})", record.EventKey, event.Binding)
                                              : std::format("{} ({})", event.Name, event.Binding);
        return std::format("{}", record.EventKey);
    }

    void DrawHistory(const AnimationPreviewSession& session)
    {
        const GameplayTagRegistry* tags = session.Tags();
        if (!ImGui::BeginTable("admissions", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Borders))
            return;
        ImGui::TableSetupColumn("Tick");
        ImGui::TableSetupColumn("What");
        ImGui::TableSetupColumn("Event");
        ImGui::TableSetupColumn("Outcome");
        ImGui::TableSetupColumn("Admission");
        ImGui::TableHeadersRow();
        std::size_t shown = 0;
        const auto& history = session.History();
        for (auto tick = history.rbegin(); tick != history.rend() && shown < 300; ++tick)
        {
            for (const AnimDecisionRecord& record : tick->Decisions)
            {
                const bool event = record.Cause == AnimDecisionCause::EventCrossed;
                const bool lifecycle = record.Cause == AnimDecisionCause::BehaviorEntered
                    || record.Cause == AnimDecisionCause::BehaviorExited;
                if (!event && !lifecycle)
                    continue;
                ++shown;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%llu", static_cast<unsigned long long>(record.Tick));
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(std::string(AnimDecisionCauseName(record.Cause)).c_str());
                ImGui::TableNextColumn();
                if (event)
                    ImGui::TextUnformatted(EventLabel(session, record).c_str());
                else
                    ImGui::TextUnformatted(tags != nullptr ? std::string(tags->GetName(record.Behavior)).c_str() : "?");
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(std::string(AnimEventOutcomeName(record.EventOutcome)).c_str());
                ImGui::TableNextColumn();
                if (record.EventOutcome == AnimEventOutcome::Fired)
                    ImGui::TextUnformatted(VerbAdmissionName(record.Admission));
                else
                    ImGui::TextDisabled("not offered");
            }
            for (const AnimationPreviewInvocation& invocation : tick->Invocations)
            {
                std::string arguments;
                for (const auto& [name, value] : invocation.Arguments)
                    arguments += std::format("{}{}={}", arguments.empty() ? "" : ", ", name, value);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("recorder");
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s(%s) via %s, by %s%s%s", invocation.Verb.c_str(), arguments.c_str(),
                                   invocation.Binding.c_str(), invocation.Producer.c_str(),
                                   invocation.Instigator.empty() ? "" : " for ", invocation.Instigator.c_str());
            }
        }
        ImGui::EndTable();
    }

    AnimationPreviewWorkspace& Workspace;
};
}

void AddAnimationEventPanels(WorkspaceView& ui, AnimationPreviewWorkspace& workspace)
{
    ui.AddPanel(std::make_unique<EventAdmissionsPanel>(workspace));
}
