#include "ui/AnimationSelectionPanels.h"

#include "authoring/AnimationPredicateText.h"
#include "authoring/AnimationPreviewWorkspace.h"
#include "authoring/AnimationPredicateEdits.h"
#include "authoring/AnimationSelectorEdits.h"
#include "ui/AnimationDocumentWidgets.h"
#include "ui/EditorUiFeature.h"
#include "ui/IEditorPanel.h"
#include "ui/ScopedPanel.h"

#include <anim/AnimSelectorData.h>
#include <anim/AnimSlotMapData.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <memory>
#include <string>
#include <vector>

namespace
{
using namespace AnimationWidgets;

const AnimationPreviewTickRecord* InspectedRecord(const AnimationPreviewWorkspace& workspace)
{
    const std::deque<AnimationPreviewTickRecord>& history = workspace.Simulation.History();
    if (history.empty())
        return nullptr;
    if (workspace.Navigation.InspectRecord && *workspace.Navigation.InspectRecord < history.size())
        return &history[*workspace.Navigation.InspectRecord];
    return &history.back();
}

std::string TagName(const AnimationPreviewWorkspace& workspace, GameplayTagId tag)
{
    const GameplayTagRegistry* tags = workspace.Simulation.Tags();
    if (!tag.IsValid() || tags == nullptr)
        return "(none)";
    return std::string(tags->GetName(tag));
}

std::string ContentPath(const AnimBoundRig& rig, std::uint16_t content)
{
    return content < rig.Contents.size() ? rig.Contents[content].Path : std::string("(no content)");
}

bool LayerPicker(AnimationPreviewWorkspace& workspace, const AnimBoundRig& rig)
{
    AnimationNavigation& nav = workspace.Navigation;
    if (rig.Layers.empty())
        return false;
    nav.Layer = std::min(nav.Layer, rig.Layers.size() - 1);
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::BeginCombo("Layer", rig.Layers[nav.Layer].NameText.c_str()))
    {
        for (std::size_t l = 0; l < rig.Layers.size(); ++l)
            if (ImGui::Selectable(rig.Layers[l].NameText.c_str(), l == nav.Layer))
            {
                nav.Layer = l;
                nav.Rule = -1;
            }
        ImGui::EndCombo();
    }
    return true;
}

class RulesPanel final : public IEditorPanel
{
public:
    explicit RulesPanel(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}
    std::string_view GetTitle() const override { return "Rules"; }
    PanelPersistence GetPersistence() const override { return { "animation.rules" }; }
    DockSlot GetDockSlot() const override { return DockSlot::Right; }
    void OnDraw() override
    {
        if (!IsVisible()) { Workspace.Sources.CancelEdits(); return; }
        ScopedPanel panel(GetTitle(), &Visible);
        if (!panel.IsOpen()) { Workspace.Sources.CancelEdits(); return; }

        AnimationPreviewSession& session = Workspace.Simulation;
        const AnimBoundRig* rig = session.Rig();
        if (rig != nullptr && LayerPicker(Workspace, *rig))
            DrawLayerRules(session, *rig);
        else
            ImGui::TextWrapped("Open a rig to see its rules decide.");

        if (DataDocument* document = Workspace.Documents.ActiveOf(kAnimSelectorType))
        {
            ImGui::SeparatorText(("Editing " + document->VirtualPath()).c_str());
            DrawEditor(*document, PredicateVocabulary{ rig });
        }
    }

private:
    void DrawLayerRules(AnimationPreviewSession& session, const AnimBoundRig& rig)
    {
        AnimationNavigation& nav = Workspace.Navigation;
        const int selectorIndex = rig.Layers[nav.Layer].Selector;
        if (selectorIndex < 0)
        {
            ImGui::TextWrapped("This layer has no selector: the newest request claiming it names its behavior, "
                               "or it plays %s.",
                               TagName(Workspace, rig.Layers[nav.Layer].Idle).c_str());
            return;
        }
        const AnimBoundSelector& selector = rig.Selectors[static_cast<std::size_t>(selectorIndex)];

        ImGui::Checkbox("Show next tick", &ShowNext);
        std::vector<AnimRuleVerdict> verdicts;
        if (ShowNext && !session.IsPlaying())
        {
            std::vector<std::vector<AnimRuleVerdict>> next = session.ExplainNextTick();
            if (nav.Layer < next.size())
                verdicts = std::move(next[nav.Layer]);
        }
        else if (const AnimationPreviewTickRecord* record = InspectedRecord(Workspace);
                 record != nullptr && nav.Layer < record->Layers.size())
        {
            verdicts = record->Layers[nav.Layer].Verdicts;
            ImGui::SameLine();
            ImGui::TextDisabled("tick %llu%s", static_cast<unsigned long long>(record->Tick),
                                nav.InspectRecord ? " (recorded)" : "");
        }

        const DataAssetCache& data = Workspace.DataCache();
        if (!ImGui::BeginTable("rules", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV
                                               | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp))
            return;
        for (const char* column : { "Band", "Rule", "Enter", "Stay", "Behavior", "This tick" })
            ImGui::TableSetupColumn(column);
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < selector.Rules.size(); ++i)
        {
            const AnimBoundRule& rule = selector.Rules[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%d", rule.PriorityBand);
            ImGui::TableNextColumn();
            if (ImGui::Selectable(rule.Label.c_str(), nav.Rule == static_cast<int>(i),
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
            {
                nav.Rule = static_cast<int>(i);
                nav.Behavior = rule.Behavior;
            }
            if (ImGui::IsItemHovered())
            {
                std::string chain;
                for (const AnimRuleSource& source : rule.Source)
                    chain += std::format("{}{} rule {} {}", chain.empty() ? "" : "\n  delegates to ", source.Selector,
                                         source.Rule, source.Name);
                ImGui::SetTooltip("%s\nhold %.0f ms, cooldown %.0f ms", chain.c_str(), rule.HoldMinMs, rule.CooldownMs);
            }
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", DescribeAnimRuleRows(data, rule.EnterRows).c_str());
            ImGui::TableNextColumn();
            if (rule.HasStay)
                ImGui::TextWrapped("%s", DescribeAnimRuleRows(data, rule.StayRows).c_str());
            else
                ImGui::TextDisabled("as enter");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(TagName(Workspace, rule.Behavior).c_str());
            ImGui::TableNextColumn();
            if (i < verdicts.size())
            {
                const bool won = verdicts[i].Kind == AnimRuleVerdictKind::Winner;
                if (won)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_PlotHistogram));
                ImGui::TextWrapped("%s", DescribeAnimVerdict(data, rule, verdicts[i]).c_str());
                if (won)
                    ImGui::PopStyleColor();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();

        if (nav.Rule >= 0 && static_cast<std::size_t>(nav.Rule) < selector.Rules.size())
        {
            const AnimBoundRule& rule = selector.Rules[static_cast<std::size_t>(nav.Rule)];
            if (ImGui::Button("Edit where it is authored") && !rule.Source.empty())
                (void)Workspace.Documents.OpenOrFocus(rule.Source.back().Selector, Workspace.DocumentError);
        }
    }

    void DrawEditor(DataDocument& document, const PredicateVocabulary& vocabulary)
    {
        ImGui::PushID(&document);
        if (ImGui::Button("Undo")) Workspace.Sources.Undo();
        ImGui::SameLine();
        if (ImGui::Button("Redo")) Workspace.Sources.Redo();
        ImGui::SameLine();
        if (ImGui::Button("Save")) Workspace.SaveDocument(Workspace.Documents.RefOf(document));
        if (const std::string status = Workspace.PreviewStatusOf(document); !status.empty())
            ImGui::TextWrapped("%s", status.c_str());

        JsonValue root = document.CopyRoot();
        JsonValue::Array* rules = AnimSelectorRules(root);
        FieldEdit edit;
        if (rules != nullptr)
        {
            for (std::size_t i = 0; i < rules->size(); ++i)
            {
                ImGui::PushID(static_cast<int>(i));
                JsonValue& rule = (*rules)[i];
                const std::string title = std::format("{} -> {}", Text(rule, "name").empty() ? "(unnamed)" : Text(rule, "name"),
                                                      Text(rule, "behavior").empty() ? "(delegates)" : Text(rule, "behavior"));
                if (ImGui::TreeNode("rule", "%s", title.c_str()))
                {
                    TextMember("Name", rule, "name", edit);
                    ImGui::SameLine();
                    int priority = static_cast<int>(Number(rule, "priority"));
                    ImGui::SetNextItemWidth(80.0f);
                    if (ImGui::DragInt("Priority", &priority))
                    {
                        SetMember(rule, "priority", JsonValue(priority));
                        edit.Changed = true;
                    }
                    edit.Committed |= ImGui::IsItemDeactivatedAfterEdit();
                    if (rule.Find("behavior") != nullptr)
                        TextMember("Behavior", rule, "behavior", edit, 220.0f);
                    else if (rule.Find("delegate") != nullptr)
                        TextMember("Delegate", rule, "delegate", edit, 260.0f);
                    else
                        TextMember("Extension point", rule, "extension", edit);
                    NumberMember("Hold ms", rule, "hold_min_ms", edit, 1.0f);
                    ImGui::SameLine();
                    NumberMember("Cooldown ms", rule, "cooldown_ms", edit, 1.0f);

                    ImGui::SeparatorText("Enter");
                    ImGui::PushID("enter");
                    DrawPredicate(*AnimSelectorPredicate(root, i, "enter"), vocabulary, edit);
                    ImGui::PopID();
                    bool ownStay = (*rules)[i].Find("stay") != nullptr;
                    if (ImGui::Checkbox("Own stay condition", &ownStay))
                    {
                        (void)SetAnimSelectorStay(root, i, ownStay);
                        edit |= FieldEdit::Instant();
                    }
                    if (ownStay && (*AnimSelectorRules(root))[i].Find("stay") != nullptr)
                    {
                        ImGui::PushID("stay");
                        DrawPredicate(*AnimSelectorPredicate(root, i, "stay"), vocabulary, edit);
                        ImGui::PopID();
                    }
                    if (ImGui::SmallButton("Move up") && i > 0)
                    {
                        (void)MoveAnimSelectorRule(root, i, i - 1);
                        edit |= FieldEdit::Instant();
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Move down"))
                    {
                        (void)MoveAnimSelectorRule(root, i, i + 1);
                        edit |= FieldEdit::Instant();
                    }
                    ImGui::SameLine();
                    const bool remove = ImGui::SmallButton("Remove rule");
                    ImGui::TreePop();
                    if (remove)
                    {
                        (void)RemoveAnimSelectorRule(root, i);
                        edit |= FieldEdit::Instant();
                        ImGui::PopID();
                        break;
                    }
                }
                ImGui::PopID();
                rules = AnimSelectorRules(root);
                if (rules == nullptr)
                    break;
            }
            if (ImGui::Button("Add rule"))
            {
                AddAnimSelectorRule(root, "new rule", "Anim.NewBehavior", 0);
                edit |= FieldEdit::Instant();
            }
        }

        ApplyFieldEdit(document, Workspace.Documents, edit, std::move(root));
        for (const DataValidationError& error : document.ValidationErrors())
            ImGui::TextWrapped("%s: %s", error.Path.c_str(), error.Message.c_str());
        ImGui::PopID();
    }

    AnimationPreviewWorkspace& Workspace;
    bool ShowNext = false;
};

class BehaviorPanel final : public IEditorPanel
{
public:
    explicit BehaviorPanel(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}
    std::string_view GetTitle() const override { return "Behavior"; }
    PanelPersistence GetPersistence() const override { return { "animation.behavior" }; }
    DockSlot GetDockSlot() const override { return DockSlot::Right; }
    void OnDraw() override
    {
        if (!IsVisible()) return;
        ScopedPanel panel(GetTitle(), &Visible);
        if (!panel.IsOpen()) return;

        const AnimBoundRig* rig = Workspace.Simulation.Rig();
        AnimationNavigation& nav = Workspace.Navigation;
        if (rig == nullptr)
        {
            ImGui::TextWrapped("Open a rig, then pick a rule or a slot row.");
            return;
        }
        if (ImGui::BeginCombo("Behavior", TagName(Workspace, nav.Behavior).c_str()))
        {
            for (const AnimBoundBehavior& behavior : rig->Behaviors)
                if (ImGui::Selectable(behavior.Name.c_str(), behavior.Tag == nav.Behavior))
                    nav.Behavior = behavior.Tag;
            ImGui::EndCombo();
        }
        if (!nav.Behavior.IsValid())
            return;

        const AnimBoundBehavior* behavior = rig->FindBehavior(nav.Behavior);
        if (behavior == nullptr)
        {
            ImGui::TextWrapped("No behavior set declares a policy for this tag: it plays as cyclic content with no latch.");
        }
        else
        {
            const AnimBehaviorDecl& policy = behavior->Policy;
            ImGui::Text("Kind: %s", AnimBehaviorKindName(policy.Kind).data());
            constexpr const char* blends[] = { "inertialize", "crossfade", "snap" };
            ImGui::Text("Blend in: %s %.0f ms%s", blends[static_cast<int>(policy.Blend.In)], policy.Blend.InMs,
                        policy.Blend.Phase == AnimPhasePolicy::Carry ? ", phase carried" : "");
            ImGui::Text("Latch: %s", AnimLatchModeName(policy.Latch.Mode).data());
            if (policy.Latch.Mode != AnimLatchMode::None)
            {
                if (policy.Latch.InterruptibleBy == AnimInterruptKind::PriorityAtLeast)
                    ImGui::BulletText("interrupted by priority %d and above", policy.Latch.Priority);
                else if (policy.Latch.InterruptibleBy == AnimInterruptKind::Tags)
                    for (const std::string& tag : policy.Latch.Tags)
                        ImGui::BulletText("interrupted by %s", tag.c_str());
                else
                    ImGui::BulletText("never interrupted");
            }
            constexpr const char* lateJoins[] = { "skip", "snap to end", "reconstruct" };
            ImGui::Text("Late join: %s%s", lateJoins[static_cast<int>(policy.LateJoin)],
                        policy.RootMotion ? ", root motion" : "");
            ImGui::Text("Cosmetic events below layer weight %.2f are suppressed", policy.EventWeight);
            for (const auto& [label, bound] : { std::pair{ "On entered", &behavior->Entered },
                                                std::pair{ "On exited", &behavior->Exited } })
            {
                if (!bound->has_value())
                {
                    ImGui::TextDisabled("%s: nothing", label);
                    continue;
                }
                ImGui::Text("%s: %s (%s)%s", label, (*bound)->BindingText.c_str(),
                            std::string(AnimEventScopeName((*bound)->Scope)).c_str(),
                            (*bound)->Resolved ? "" : " -- unresolved, see Problems");
            }
            ImGui::TextDisabled("Declared in %s", behavior->DeclaredIn.c_str());
            if (ImGui::SmallButton("Edit behavior set"))
                (void)Workspace.Documents.OpenOrFocus(behavior->DeclaredIn, Workspace.DocumentError);
        }

        ImGui::SeparatorText("Selected by");
        for (std::size_t l = 0; l < rig->Layers.size(); ++l)
        {
            const int selector = rig->Layers[l].Selector;
            if (selector < 0)
                continue;
            const std::vector<AnimBoundRule>& rules = rig->Selectors[static_cast<std::size_t>(selector)].Rules;
            for (std::size_t r = 0; r < rules.size(); ++r)
            {
                if (rules[r].Behavior != nav.Behavior)
                    continue;
                ImGui::PushID(static_cast<int>(l * 1000 + r));
                if (ImGui::Selectable(std::format("{}: {}", rig->Layers[l].NameText, rules[r].Label).c_str(),
                                      nav.Layer == l && nav.Rule == static_cast<int>(r)))
                {
                    nav.Layer = l;
                    nav.Rule = static_cast<int>(r);
                }
                ImGui::PopID();
            }
        }
        ImGui::SeparatorText("Resolved by");
        for (std::size_t r = 0; r < rig->SlotRows.size(); ++r)
        {
            const AnimBoundSlotRow& row = rig->SlotRows[r];
            if (row.Behavior != nav.Behavior)
                continue;
            ImGui::PushID(static_cast<int>(r));
            if (ImGui::Selectable(std::format("{} (row {} of {})", ContentPath(*rig, static_cast<std::uint16_t>(row.Content)),
                                              row.Index, row.DeclaredIn).c_str(),
                                  nav.Row == static_cast<int>(r)))
            {
                nav.Row = static_cast<int>(r);
                nav.Content = row.Content;
            }
            ImGui::PopID();
        }
    }

private:
    AnimationPreviewWorkspace& Workspace;
};

class SlotMapPanel final : public IEditorPanel
{
public:
    explicit SlotMapPanel(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}
    std::string_view GetTitle() const override { return "Slot map"; }
    PanelPersistence GetPersistence() const override { return { "animation.slot_map" }; }
    DockSlot GetDockSlot() const override { return DockSlot::Bottom; }
    void OnDraw() override
    {
        if (!IsVisible()) return;
        ScopedPanel panel(GetTitle(), &Visible);
        if (!panel.IsOpen()) return;

        const AnimBoundRig* rig = Workspace.Simulation.Rig();
        AnimationNavigation& nav = Workspace.Navigation;
        if (rig == nullptr)
        {
            ImGui::TextWrapped("Open a rig to see its effective slot map.");
            return;
        }
        ImGui::Checkbox("Only the selected behavior", &OnlySelected);
        ImGui::SameLine();
        ImGui::TextDisabled("Merged across the rig's slot maps: priority first, then stack order.");

        const AnimContentState* content = Workspace.Simulation.Content();
        const DataAssetCache& data = Workspace.DataCache();
        if (ImGui::BeginTable("rows", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable))
        {
            for (const char* column : { "Priority", "Behavior", "When", "Clip", "From", "Playing" })
                ImGui::TableSetupColumn(column);
            ImGui::TableHeadersRow();
            for (std::size_t r = 0; r < rig->SlotRows.size(); ++r)
            {
                const AnimBoundSlotRow& row = rig->SlotRows[r];
                if (OnlySelected && nav.Behavior.IsValid() && row.Behavior != nav.Behavior)
                    continue;
                ImGui::PushID(static_cast<int>(r));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%d", row.Priority);
                ImGui::TableNextColumn();
                if (ImGui::Selectable(row.BehaviorName.c_str(), nav.Row == static_cast<int>(r),
                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
                {
                    nav.Row = static_cast<int>(r);
                    nav.Behavior = row.Behavior;
                    nav.Content = row.Content;
                }
                ImGui::TableNextColumn();
                const AnimSlotMapData* map = data.TryGet<AnimSlotMapData>(data.Find(row.DeclaredIn), kAnimSlotMapType);
                ImGui::TextWrapped("%s", map != nullptr && row.Index < map->Rows.size()
                                             ? DescribeAnimPredicate(map->Rows[row.Index].When).c_str()
                                             : "?");
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(ContentPath(*rig, static_cast<std::uint16_t>(row.Content)).c_str());
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s #%u", row.DeclaredIn.c_str(), row.Index);
                ImGui::TableNextColumn();
                if (content != nullptr)
                    for (std::size_t l = 0; l < rig->Layers.size() && l < kAnimMaxLayers; ++l)
                        if (content->Layers[l].Row == r)
                            ImGui::Text("%s  %.2fs", rig->Layers[l].NameText.c_str(), content->Layers[l].TimeSeconds);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        if (nav.Row >= 0 && static_cast<std::size_t>(nav.Row) < rig->SlotRows.size())
        {
            const AnimBoundSlotRow& row = rig->SlotRows[static_cast<std::size_t>(nav.Row)];
            if (ImGui::Button("Audition this clip"))
                Workspace.AuditionClip(ContentPath(*rig, static_cast<std::uint16_t>(row.Content)));
            ImGui::SameLine();
            if (ImGui::Button("Edit slot map"))
                (void)Workspace.Documents.OpenOrFocus(row.DeclaredIn, Workspace.DocumentError);
        }
        if (Workspace.Viewport.Source == AnimationViewportSource::Audition && Workspace.Simulation.IsOpen())
        {
            ImGui::SameLine();
            if (ImGui::Button("Back to the simulation"))
                Workspace.Viewport.Source = AnimationViewportSource::Simulation;
        }
    }

private:
    AnimationPreviewWorkspace& Workspace;
    bool OnlySelected = true;
};

class DecisionsPanel final : public IEditorPanel
{
public:
    explicit DecisionsPanel(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}
    std::string_view GetTitle() const override { return "Decisions"; }
    PanelPersistence GetPersistence() const override { return { "animation.decisions" }; }
    DockSlot GetDockSlot() const override { return DockSlot::Bottom; }
    void OnDraw() override
    {
        if (!IsVisible()) return;
        ScopedPanel panel(GetTitle(), &Visible);
        if (!panel.IsOpen()) return;

        AnimationPreviewSession& session = Workspace.Simulation;
        const AnimBoundRig* rig = session.Rig();
        const std::deque<AnimationPreviewTickRecord>& history = session.History();
        AnimationNavigation& nav = Workspace.Navigation;
        if (rig == nullptr || history.empty())
        {
            ImGui::TextWrapped("Run a rig to record its decisions.");
            return;
        }

        bool live = !nav.InspectRecord.has_value();
        if (ImGui::Checkbox("Live", &live))
            nav.InspectRecord = live ? std::nullopt : std::optional<std::size_t>(history.size() - 1);
        if (nav.InspectRecord)
        {
            int index = static_cast<int>(std::min(*nav.InspectRecord, history.size() - 1));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::SliderInt("##tick", &index, 0, static_cast<int>(history.size() - 1), "recorded %d"))
                nav.InspectRecord = static_cast<std::size_t>(index);
            ImGui::TextDisabled("Inspecting a recorded tick; the live session is unchanged.");
        }
        const AnimationPreviewTickRecord* record = InspectedRecord(Workspace);
        ImGui::Text("Tick %llu%s", static_cast<unsigned long long>(record->Tick),
                    record->FactsExact ? "" : "  (derived facts warming up)");

        const DataAssetCache& data = Workspace.DataCache();
        for (std::size_t l = 0; l < record->Layers.size() && l < rig->Layers.size(); ++l)
        {
            const AnimationPreviewLayerRecord& layer = record->Layers[l];
            ImGui::PushID(static_cast<int>(l));
            const int selector = rig->Layers[l].Selector;
            const std::string winner = selector >= 0 && layer.Winner != kAnimNoRule
                ? rig->Selectors[static_cast<std::size_t>(selector)].Rules[layer.Winner].Label
                : std::string(selector >= 0 ? "(none)" : "request-keyed");
            constexpr const char* latches[] = { "", ", latched", ", latch finishing", ", latch cancelling" };
            if (ImGui::TreeNodeEx("layer", ImGuiTreeNodeFlags_DefaultOpen, "%s: %s -> %s%s, %s at %.2fs%s",
                                  rig->Layers[l].NameText.c_str(), winner.c_str(),
                                  TagName(Workspace, layer.Behavior).c_str(), latches[static_cast<int>(layer.Latch)],
                                  ContentPath(*rig, layer.Content).c_str(), layer.TimeSeconds,
                                  layer.ContentComplete ? ", complete" : ""))
            {
                if (selector >= 0)
                {
                    const std::vector<AnimBoundRule>& rules = rig->Selectors[static_cast<std::size_t>(selector)].Rules;
                    for (std::size_t r = 0; r < rules.size() && r < layer.Verdicts.size(); ++r)
                    {
                        ImGui::PushID(static_cast<int>(r));
                        if (ImGui::Selectable(std::format("{}: {}", rules[r].Label,
                                                          DescribeAnimVerdict(data, rules[r], layer.Verdicts[r])).c_str(),
                                              nav.Layer == l && nav.Rule == static_cast<int>(r)))
                        {
                            nav.Layer = l;
                            nav.Rule = static_cast<int>(r);
                            nav.Behavior = rules[r].Behavior;
                        }
                        ImGui::PopID();
                    }
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }

        if (!record->Decisions.empty())
        {
            ImGui::SeparatorText("Recorded on this tick");
            for (const AnimDecisionRecord& decision : record->Decisions)
            {
                std::string text = std::format("{} ({})", AnimDecisionCauseName(decision.Cause),
                                               AnimChangeReasonName(decision.Reason));
                if (decision.Layer != kAnimNoLayer && decision.Layer < rig->Layers.size())
                    text = rig->Layers[decision.Layer].NameText + ": " + text;
                if (decision.Behavior.IsValid())
                    text += " " + TagName(Workspace, decision.Behavior);
                ImGui::BulletText("%s", text.c_str());
            }
        }
    }

private:
    AnimationPreviewWorkspace& Workspace;
};
}

void AddAnimationSelectionPanels(EditorUiFeature& ui, AnimationPreviewWorkspace& workspace)
{
    ui.AddPanel(std::make_unique<RulesPanel>(workspace));
    ui.AddPanel(std::make_unique<BehaviorPanel>(workspace));
    ui.AddPanel(std::make_unique<SlotMapPanel>(workspace));
    ui.AddPanel(std::make_unique<DecisionsPanel>(workspace));
}
