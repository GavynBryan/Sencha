#include "ui/AnimationLayerPanels.h"

#include "authoring/AnimationFlowEdits.h"
#include "authoring/AnimationPredicateText.h"
#include "authoring/AnimationPreviewWorkspace.h"
#include "ui/DocumentSaveReportView.h"
#include "authoring/AnimationPredicateEdits.h"
#include "ui/AnimationDocumentWidgets.h"
#include "ui/WorkspaceView.h"
#include "ui/IEditorPanel.h"
#include "ui/ScopedPanel.h"

#include <anim/AnimFlowData.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <imgui.h>

#include <algorithm>
#include <format>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace
{
// The inspected record, else the latest.
const AnimationPreviewTickRecord* ShownTick(const AnimationPreviewWorkspace& workspace)
{
    const auto& history = workspace.Rig.Simulation.History();
    if (history.empty())
        return nullptr;
    const std::optional<std::size_t> inspect = workspace.Rig.Navigation.InspectRecord;
    return inspect && *inspect < history.size() ? &history[*inspect] : &history.back();
}

class FlowPanel final : public IEditorPanel
{
public:
    explicit FlowPanel(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}
    std::string_view GetTitle() const override { return "Flow"; }
    PanelPersistence GetPersistence() const override { return { "animation.flow" }; }
    DockSlot GetDockSlot() const override { return DockSlot::Bottom; }
    void OnDraw() override
    {
        if (!IsVisible()) return;
        ScopedPanel panel(GetWindowName(), &Visible);
        if (!panel.IsOpen()) return;

        const AnimationPreviewSession& session = Workspace.Rig.Simulation;
        const AnimBoundRig* rig = session.Rig();
        const AnimationPreviewTickRecord* tick = ShownTick(Workspace);
        const std::size_t l = Workspace.Rig.Navigation.Layer;
        const AnimationPreviewLayerRecord* layer =
            rig != nullptr && tick != nullptr && l < tick->Layers.size() ? &tick->Layers[l] : nullptr;
        const AnimBoundFlow* playing = layer != nullptr && layer->Content < rig->Contents.size()
                && rig->Contents[layer->Content].Flow >= 0
            ? &rig->Flows[static_cast<std::size_t>(rig->Contents[layer->Content].Flow)]
            : nullptr;

        DataDocument* document = Workspace.Documents.ActiveOf(kAnimFlowType);
        if (document == nullptr && playing != nullptr)
            document = Workspace.Documents.Find(playing->Path);
        if (playing != nullptr)
        {
            const DataAssetCache& data = Workspace.DataCache();
            const AnimFlowData* authored = data.TryGet<AnimFlowData>(data.Find(playing->Path), kAnimFlowType);
            ImGui::Text("%s on %s", playing->Path.c_str(), rig->Layers[l].NameText.c_str());
            if (document == nullptr)
            {
                ImGui::SameLine();
                if (ImGui::SmallButton("Edit"))
                    (void)(void)Workspace.Documents.OpenOrFocus(playing->Path, Workspace.DocumentError);
            }
            const double tickSeconds = 1.0 / std::max(1u, session.Scenario().TickRate);
            DrawStrip(*rig, *playing, *layer, tick->Tick, tickSeconds);
            DrawControl(*playing, authored);
        }
        else if (rig != nullptr && l < rig->Layers.size())
            ImGui::TextDisabled("%s is not playing a flow.", rig->Layers[l].NameText.c_str());
        else
            ImGui::TextDisabled("Run a rig to see the flow its selected layer plays.");

        if (document != nullptr)
            DrawEditor(*document);
    }

private:
    static float SectionSeconds(const AnimBoundRig& rig, const AnimBoundFlowSection& section,
                                const AnimationPreviewLayerRecord& layer, bool current)
    {
        // A slot section's length is known only while it plays.
        const int content = section.Content >= 0 ? section.Content
            : current && layer.Clip < rig.Contents.size() ? static_cast<int>(layer.Clip)
                                                          : -1;
        return content >= 0 ? rig.Contents[static_cast<std::size_t>(content)].DurationSeconds : 0.0f;
    }

    void DrawStrip(const AnimBoundRig& rig, const AnimBoundFlow& flow, const AnimationPreviewLayerRecord& layer,
                   AnimTick now, double tickSeconds)
    {
        const std::size_t count = flow.Sections.size();
        std::vector<float> seconds(count);
        float total = 0.0f;
        for (std::size_t s = 0; s < count; ++s)
        {
            const bool current = layer.Flow.Phase != AnimFlowPhase::None && layer.Flow.Section == s;
            seconds[s] = std::max(SectionSeconds(rig, flow.Sections[s], layer, current), 0.05f);
            total += seconds[s];
        }
        const float height = ImGui::GetTextLineHeightWithSpacing() * 1.6f;
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float width = std::max(ImGui::GetContentRegionAvail().x, 120.0f);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImU32 idle = ImGui::GetColorU32(ImGuiCol_FrameBg);
        const ImU32 active = ImGui::GetColorU32(ImGuiCol_PlotHistogram);
        const ImU32 edge = ImGui::GetColorU32(ImGuiCol_Border);
        const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);

        float x = origin.x;
        float cancelX = -1.0f;
        float cancelWidth = 0.0f;
        for (std::size_t s = 0; s < count; ++s)
        {
            const AnimBoundFlowSection& section = flow.Sections[s];
            const float w = width * seconds[s] / total;
            const bool current = layer.Flow.Phase != AnimFlowPhase::None && layer.Flow.Section == s;
            const ImVec2 min(x + 1.0f, origin.y);
            const ImVec2 max(x + w - 1.0f, origin.y + height);
            draw->AddRectFilled(min, max, current ? active : idle, 3.0f);
            draw->AddRect(min, max, edge, 3.0f);
            if (current && layer.Flow.Phase == AnimFlowPhase::Playing)
            {
                const double elapsed = static_cast<double>(now - std::min(now, layer.Flow.SectionStartTick)) * tickSeconds;
                const float t = std::clamp(static_cast<float>(elapsed / seconds[s]), 0.0f, 1.0f);
                draw->AddLine(ImVec2(min.x + (max.x - min.x) * t, min.y), ImVec2(min.x + (max.x - min.x) * t, max.y),
                              text, 2.0f);
            }
            std::string label = section.TagName;
            if (section.Loop != AnimFlowLoop::Once)
                label += current ? std::format(" x{}", layer.Flow.LoopCount + 1) : std::string(" (loops)");
            if (section.CancelTiming == AnimCancelTiming::Immediate)
                label += " !";
            if (section.Ends)
                label += " |";
            draw->PushClipRect(min, max, true);
            draw->AddText(ImVec2(min.x + 4.0f, min.y + 3.0f), text, label.c_str());
            draw->PopClipRect();
            if (flow.Cancel == static_cast<int>(s))
            {
                cancelX = x;
                cancelWidth = w;
            }
            x += w;
        }
        const float laneY = origin.y + height + 4.0f;
        draw->AddText(ImVec2(origin.x, laneY), ImGui::GetColorU32(ImGuiCol_TextDisabled), "cancel");
        if (cancelX >= 0.0f)
        {
            const ImVec2 min(cancelX + 1.0f, laneY + ImGui::GetTextLineHeight());
            const ImVec2 max(cancelX + cancelWidth - 1.0f, laneY + ImGui::GetTextLineHeight() + height * 0.6f);
            draw->AddRect(min, max, edge, 3.0f, 0, 2.0f);
        }
        ImGui::Dummy(ImVec2(width, height * 1.8f + ImGui::GetTextLineHeight() + 8.0f));
        ImGui::TextDisabled("! cancels immediately   | ends the flow   xN loop pass");
        if (layer.Flow.Phase == AnimFlowPhase::Complete)
            ImGui::Text("Complete, holding the last pose.");
    }

    static void DrawControl(const AnimBoundFlow& flow, const AnimFlowData* authored)
    {
        if (authored == nullptr)
            return;
        ImGui::SeparatorText("How each section is left");
        for (std::size_t s = 0; s < authored->Sections.size(); ++s)
        {
            const AnimFlowSectionDecl& section = authored->Sections[s];
            std::string text = section.Tag + ": ";
            if (section.Loop == AnimFlowLoop::While)
                text += std::format("repeats while {}; ", DescribeAnimPredicate(section.While));
            else if (section.Loop == AnimFlowLoop::Count)
                text += std::format("plays {}.{} times; ", section.CountIntent, section.CountParam);
            for (const AnimFlowBranchDecl& branch : section.Branches)
                text += std::format("to {} when {}; ", branch.To, DescribeAnimPredicate(branch.When));
            text += section.Ends ? "otherwise ends the flow" : s + 1 < authored->Sections.size() ? "otherwise next"
                                                                                                    : "then ends";
            if (section.CancelTiming == AnimCancelTiming::Immediate)
                text += "; a cancel leaves at once";
            ImGui::BulletText("%s", text.c_str());
        }
        ImGui::TextWrapped("Cancel section: %s. Control only goes forward: a sequence that needs to go back "
                           "is a decision for gameplay, answered with a new request.",
                           flow.Cancel >= 0 ? authored->Sections[static_cast<std::size_t>(flow.Cancel)].Tag.c_str()
                                            : "none, a cancel ends the flow");
    }

    // Structural changes are one undo step each; conditions commit on release.
    void DrawEditor(DataDocument& document)
    {
        ImGui::SeparatorText(std::format("Editing {}", document.VirtualPath()).c_str());
        ImGui::PushID(&document);
        if (ImGui::SmallButton("Undo")) Workspace.Sources.Undo();
        ImGui::SameLine();
        if (ImGui::SmallButton("Redo")) Workspace.Sources.Redo();
        ImGui::SameLine();
        if (ImGui::SmallButton("Save")) Workspace.DocumentError = DescribeDocumentSave(Workspace.Sources.Save(Workspace.Documents.RefOf(document)));
        if (document.IsDirty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("unsaved");
        }

        JsonValue root = document.CopyRoot();
        JsonValue::Array* sections = AnimFlowSections(root);
        if (sections == nullptr)
        {
            ImGui::PopID();
            return;
        }
        bool changed = false;
        FieldEdit edit;
        const AnimationWidgets::PredicateVocabulary vocabulary{ Workspace.Rig.Simulation.Rig() };
        const auto tagOf = [&](std::size_t s) {
            const JsonValue* tag = (*sections)[s].Find("tag");
            return tag != nullptr && tag->IsString() ? tag->AsString() : std::string();
        };
        for (std::size_t s = 0; s < sections->size() && !changed; ++s)
        {
            JsonValue& section = (*sections)[s];
            ImGui::PushID(static_cast<int>(s));
            const std::string tag = tagOf(s);
            const JsonValue* loopValue = section.Find("loop");
            const std::string loop = loopValue != nullptr && loopValue->IsString() ? loopValue->AsString() : "once";
            if (ImGui::TreeNodeEx("section", ImGuiTreeNodeFlags_DefaultOpen, "%zu  %s", s, tag.c_str()))
            {
                if (ImGui::BeginCombo("Loop", loop.c_str()))
                {
                    for (const char* kind : { "once", "while", "count" })
                        if (ImGui::Selectable(kind, loop == kind))
                            changed = SetAnimFlowLoop(root, s, kind);
                    ImGui::EndCombo();
                }
                if (loop == "while")
                {
                    ImGui::TextDisabled("Repeats while, at its end:");
                    ImGui::PushID("while");
                    if (JsonValue::Array* rows = AnimFlowLoopCondition(root, s))
                        AnimationWidgets::DrawPredicate(*rows, vocabulary, edit);
                    ImGui::PopID();
                }
                else if (loop == "count")
                {
                    AnimationWidgets::NameMember("Count intent", section, "count_intent", vocabulary.Intents(), edit);
                    AnimationWidgets::NameMember("Count parameter", section, "count_param",
                                                 vocabulary.Params(AnimationWidgets::Text(section, "count_intent")),
                                                 edit);
                }
                const JsonValue* endsValue = section.Find("ends");
                bool ends = endsValue != nullptr && endsValue->IsBool() && endsValue->AsBool();
                if (ImGui::Checkbox("Ends the flow when no branch is taken", &ends))
                    changed = SetAnimFlowEnds(root, s, ends);
                const JsonValue* timing = section.Find("cancel_timing");
                bool immediate = timing != nullptr && timing->IsString() && timing->AsString() == "immediate";
                if (ImGui::Checkbox("A cancel leaves this section at once", &immediate))
                    changed = SetAnimFlowCancelImmediately(root, s, immediate);

                const JsonValue* branches = section.Find("branches");
                if (branches != nullptr && branches->IsArray())
                    for (std::size_t b = 0; b < branches->AsArray().size() && !changed; ++b)
                    {
                        const std::string to = AnimationWidgets::Text(branches->AsArray()[b], "to");
                        ImGui::PushID(static_cast<int>(b));
                        ImGui::BulletText("to %s, taken at the end when:", to.c_str());
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Remove branch"))
                            changed = RemoveAnimFlowBranch(root, s, b);
                        else if (JsonValue::Array* rows = AnimFlowBranchCondition(root, s, b))
                        {
                            ImGui::Indent();
                            AnimationWidgets::DrawPredicate(*rows, vocabulary, edit);
                            ImGui::Unindent();
                        }
                        ImGui::PopID();
                    }
                // Only later sections: control never goes backward.
                if (!changed && s + 1 < sections->size() && ImGui::BeginCombo("Add branch to", "later section..."))
                {
                    for (std::size_t to = s + 1; to < sections->size(); ++to)
                        if (ImGui::Selectable(tagOf(to).c_str()))
                            changed = AddAnimFlowBranch(root, s, to);
                    ImGui::EndCombo();
                }
                if (!changed && ImGui::SmallButton("Move up") && s > 0)
                    changed = MoveAnimFlowSection(root, s, s - 1);
                ImGui::SameLine();
                if (!changed && ImGui::SmallButton("Move down"))
                    changed = MoveAnimFlowSection(root, s, s + 1);
                ImGui::SameLine();
                if (!changed && ImGui::SmallButton("Remove section"))
                    changed = RemoveAnimFlowSection(root, s);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }

        if (!changed)
        {
            JsonValue* data = root.Find("data");
            const JsonValue* cancelValue = data->Find("cancel");
            const std::string cancel = cancelValue != nullptr && cancelValue->IsString() ? cancelValue->AsString() : "";
            if (ImGui::BeginCombo("Cancel section", cancel.empty() ? "none: a cancel ends the flow" : cancel.c_str()))
            {
                if (ImGui::Selectable("none", cancel.empty()))
                    changed = SetAnimFlowCancel(root, {});
                for (std::size_t s = 0; s < sections->size(); ++s)
                    if (ImGui::Selectable(tagOf(s).c_str(), tagOf(s) == cancel))
                        changed = SetAnimFlowCancel(root, tagOf(s));
                ImGui::EndCombo();
            }
        }
        if (!changed && !Workspace.Content.Of(AssetType::AnimationClip).empty() && ImGui::BeginCombo("Add section playing", "clip..."))
        {
            for (const std::string& clip : Workspace.Content.Of(AssetType::AnimationClip))
                if (ImGui::Selectable(clip.c_str()))
                {
                    AddAnimFlowSection(root, std::format("Anim.Section.S{}", sections->size()), clip);
                    changed = true;
                }
            ImGui::EndCombo();
        }
        if (changed)
            edit |= FieldEdit::Instant();
        ApplyFieldEdit(document, Workspace.Documents, edit, std::move(root));
        for (const DataValidationError& error : document.ValidationErrors())
            ImGui::TextWrapped("%s: %s", error.Path.c_str(), error.Message.c_str());
        ImGui::PopID();
    }

    AnimationPreviewWorkspace& Workspace;
};
}

void AddAnimationLayerPanels(WorkspaceView& ui, AnimationPreviewWorkspace& workspace)
{
    ui.AddPanel(std::make_unique<FlowPanel>(workspace));
}
