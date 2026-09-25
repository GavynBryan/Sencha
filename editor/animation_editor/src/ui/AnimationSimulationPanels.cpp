#include "ui/AnimationSimulationPanels.h"

#include "authoring/AnimationPreviewWorkspace.h"
#include "authoring/AnimationRigOutline.h"
#include "ui/EditorUiFeature.h"
#include "ui/IEditorPanel.h"
#include "ui/ScopedPanel.h"

#include <anim/AnimFactEvaluation.h>
#include <anim/AnimationClipCache.h>
#include <anim/SkeletonCache.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
constexpr std::array<const char*, 3> kLifetimes{ "Held", "Fixed", "Impulse" };
constexpr std::array<const char*, 4> kCancelReasons{ "Released", "Interrupted", "Failed", "Superseded" };

// A text field over a std::string, committing on Enter. Returns true when the
// author submitted it.
bool SubmitText(const char* label, std::string& text)
{
    std::array<char, 256> buffer{};
    const std::size_t length = std::min(text.size(), buffer.size() - 1);
    std::memcpy(buffer.data(), text.data(), length);
    const bool submitted =
        ImGui::InputText(label, buffer.data(), buffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);
    text = buffer.data();
    return submitted;
}

std::string TagName(const AnimationPreviewSession& session, std::uint32_t bits)
{
    const GameplayTagRegistry* tags = session.Tags();
    if (bits == 0 || tags == nullptr)
        return "(none)";
    const std::string_view name = tags->GetName(GameplayTagId{ bits });
    return name.empty() ? std::format("#{}", bits) : std::string(name);
}

std::string FormatFact(const AnimationPreviewSession& session, AnimFactKind kind, std::uint32_t bits)
{
    switch (kind)
    {
    case AnimFactKind::Bool: return AnimFactToBool(bits) ? "true" : "false";
    case AnimFactKind::Float: return std::format("{:.3f}", AnimFactToFloat(bits));
    case AnimFactKind::Int: return std::format("{}", AnimFactToInt(bits));
    case AnimFactKind::Tag: return TagName(session, bits);
    case AnimFactKind::TagSet: return AnimFactToBool(bits) ? "has tags" : "no container";
    }
    return {};
}

std::string DescribeDerivation(const AnimBoundRig& rig, const AnimBoundDerivation& derivation)
{
    std::string sources;
    for (std::size_t i = 0; i < derivation.SourceCount; ++i)
    {
        if (i > 0)
            sources += ", ";
        if (derivation.Sources[i].Negate)
            sources += "not ";
        sources += rig.Slots[derivation.Sources[i].Slot].Name;
    }
    std::string text = std::format("{}({})", AnimDerivationOpName(derivation.Op), sources);
    if (derivation.WindowMs > 0.0f)
        text += std::format(" over {:.0f} ms", derivation.WindowMs);
    return text;
}

std::string IntentName(const AnimationPreviewSession& session, GameplayTagId intent)
{
    if (const AnimBoundRig* rig = session.Rig())
    {
        if (const AnimBoundIntent* bound = rig->FindIntent(intent))
            return bound->Name;
    }
    return TagName(session, intent.Value);
}

class RigScenarioPanel final : public IEditorPanel
{
public:
    explicit RigScenarioPanel(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}
    std::string_view GetTitle() const override { return "Rig and scenario"; }
    PanelPersistence GetPersistence() const override { return { "animation.rig" }; }
    DockSlot GetDockSlot() const override { return DockSlot::Left; }
    void OnDraw() override
    {
        if (!IsVisible()) return;
        ScopedPanel panel(GetTitle(), &Visible);
        if (!panel.IsOpen()) return;

        if (ImGui::CollapsingHeader("Rigs", ImGuiTreeNodeFlags_DefaultOpen))
        {
            for (const std::string& path : Workspace.RigPaths)
                if (ImGui::Selectable(path.c_str(), path == Workspace.RigPath)) Workspace.OpenRig(path);
            if (Workspace.RigPaths.empty()) ImGui::TextDisabled("No animation.rig assets in the mounted project.");
        }
        if (ImGui::CollapsingHeader("New rig"))
            DrawNewRig();
        if (!Workspace.ScenarioError.empty())
            ImGui::TextWrapped("%s", Workspace.ScenarioError.c_str());

        AnimationPreviewSession& session = Workspace.Simulation;
        if (!session.IsOpen() || Workspace.RigPath.empty()) return;

        if (ImGui::CollapsingHeader("Scenario", ImGuiTreeNodeFlags_DefaultOpen))
        {
            const AnimationScenario& scenario = session.Scenario();
            ImGui::TextWrapped("%s%s", scenario.Name.c_str(), session.ScenarioModified() ? " (unsaved)" : "");
            ImGui::TextDisabled("%s", Workspace.ScenarioFile.c_str());
            ImGui::Text("%u Hz, %zu actions", scenario.TickRate, scenario.Actions.size());
            if (ImGui::Button("Save scenario")) Workspace.SaveScenario();
            ImGui::SameLine();
            if (ImGui::Button("Revert to saved")) Workspace.ReloadScenario();
            ImGui::TextWrapped("Live edits are recorded into the scenario on the next tick. Saving is "
                               "explicit and never writes the rig or its schemas.");

            ImGui::SeparatorText("Participants");
            for (const std::string& participant : scenario.Participants)
                ImGui::BulletText("%s", participant.c_str());
            if (SubmitText("Add participant", NewParticipant) && session.AddParticipant(NewParticipant))
            {
                NewParticipant.clear();
                session.Restart();
            }
            ImGui::SeparatorText("Fixture tags");
            ImGui::TextWrapped("Names a game module would declare, declared by this scenario instead; they exist "
                               "only in the preview World. Changing them replays to the current tick.");
            std::string removed;
            for (const std::string& tag : scenario.DeclaredTags)
            {
                ImGui::PushID(tag.c_str());
                ImGui::BulletText("%s", tag.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("remove"))
                    removed = tag;
                ImGui::PopID();
            }
            if (!removed.empty())
                (void)session.UndeclareTag(removed);
            if (SubmitText("Declare tag", NewTag))
            {
                if (session.DeclareTag(NewTag, &TagError))
                {
                    NewTag.clear();
                    TagError.clear();
                }
            }
            if (!TagError.empty())
                ImGui::TextWrapped("%s", TagError.c_str());
        }

        if (ImGui::CollapsingHeader("Dependencies", ImGuiTreeNodeFlags_DefaultOpen))
        {
            for (const AnimationRigDependency& row :
                 DescribeAnimationRigDependencies(Workspace.DataCache(), Workspace.RigPath))
            {
                const char* status = "";
                switch (row.Status)
                {
                case AnimationRigDependency::State::Resident: status = "loaded"; break;
                case AnimationRigDependency::State::Missing: status = "MISSING"; break;
                case AnimationRigDependency::State::WrongSubtype: status = "WRONG TYPE"; break;
                case AnimationRigDependency::State::OtherKind: status = ""; break;
                }
                ImGui::Indent(static_cast<float>(row.Depth) * 12.0f);
                ImGui::TextWrapped("%s: %s %s", row.Role.c_str(), row.Path.c_str(), status);
                ImGui::Unindent(static_cast<float>(row.Depth) * 12.0f);
            }
        }

    }
private:
    // A name, the tier it starts as, and the clips to play in the order
    // ticked: the first idles; what the rest do depends on the tier.
    void DrawNewRig()
    {
        ImGui::TextWrapped("Writes a behavior set, slot map, request schema, selectors, rig and scenario into "
                           "the project, then opens the rig ready to play.");
        (void)SubmitText("Name", NewRigName);
        static constexpr const char* kPresetHelp[] = {
            "Prop: one layer, no selector. The first clip idles; every other clip plays while a request of its "
            "name is held. Doors, machinery, pickups.",
            "Simple: one layer chosen by rules over the engine's facts. The first clip idles, the second plays "
            "while moving, and the rest are actions a request plays once through. Most enemies.",
            "Character: Simple's idle and locomotion, and an upper-body layer from a chosen joint that plays the "
            "actions over them. Players and anything that acts while it moves.",
        };
        int preset = static_cast<int>(NewRigPreset);
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::Combo("Tier", &preset, "Prop\0Simple\0Character\0"))
            NewRigPreset = static_cast<AnimationRigPreset>(preset);
        ImGui::TextDisabled("%s", kPresetHelp[preset]);
        const AnimationClipCache& clips = Workspace.Clips();
        const auto skeletonOf = [&](const std::string& path) {
            const AnimationClipData* clip = clips.Get(clips.Find(path));
            return clip != nullptr ? clip->SkeletonPath : std::string();
        };
        const std::string skeleton = NewRigClips.empty() ? std::string() : skeletonOf(NewRigClips.front());
        if (ImGui::BeginChild("clips", ImVec2(0.0f, 140.0f), ImGuiChildFlags_Borders))
        {
            for (const std::string& path : Workspace.ClipPaths)
            {
                const auto chosen = std::find(NewRigClips.begin(), NewRigClips.end(), path);
                bool ticked = chosen != NewRigClips.end();
                // One rig poses one skeleton: once a clip is chosen, clips of
                // another cannot join it.
                ImGui::BeginDisabled(!ticked && !skeleton.empty() && skeletonOf(path) != skeleton);
                const std::string label = ticked && chosen == NewRigClips.begin() ? path + "  (idle)" : path;
                if (ImGui::Checkbox(label.c_str(), &ticked))
                {
                    if (ticked)
                        NewRigClips.push_back(path);
                    else
                        NewRigClips.erase(chosen);
                }
                ImGui::EndDisabled();
            }
        }
        ImGui::EndChild();
        if (NewRigPreset == AnimationRigPreset::Character)
        {
            // The joints of the chosen clips' skeleton, by name.
            const SkeletonData* data = skeleton.empty()
                ? nullptr
                : Workspace.Skeletons().Get(Workspace.Skeletons().Find(skeleton));
            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::BeginCombo("Upper body from", NewRigUpperJoint.empty() ? "(choose a joint)"
                                                                               : NewRigUpperJoint.c_str()))
            {
                if (data != nullptr)
                    for (const SkeletonJoint& joint : data->Joints)
                        if (ImGui::Selectable(joint.Name.c_str(), joint.Name == NewRigUpperJoint))
                            NewRigUpperJoint = joint.Name;
                ImGui::EndCombo();
            }
            if (data == nullptr)
                ImGui::TextDisabled("Choose clips first; the joints are their skeleton's.");
        }
        ImGui::BeginDisabled(NewRigName.empty() || NewRigClips.empty());
        if (ImGui::Button("Create rig"))
        {
            if (Workspace.CreateRig({ .Name = NewRigName,
                                      .Clips = NewRigClips,
                                      .Preset = NewRigPreset,
                                      .UpperBodyJoint = NewRigUpperJoint },
                                    NewRigError))
            {
                NewRigName.clear();
                NewRigClips.clear();
                NewRigUpperJoint.clear();
            }
        }
        ImGui::EndDisabled();
        if (!NewRigError.empty())
            ImGui::TextWrapped("%s", NewRigError.c_str());
    }

    AnimationPreviewWorkspace& Workspace;
    std::string NewParticipant;
    std::string NewTag;
    std::string TagError;
    std::string NewRigName;
    std::vector<std::string> NewRigClips;
    AnimationRigPreset NewRigPreset = AnimationRigPreset::Prop;
    std::string NewRigUpperJoint;
    std::string NewRigError;
};

class FactsPanel final : public IEditorPanel
{
public:
    explicit FactsPanel(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}
    std::string_view GetTitle() const override { return "Facts"; }
    PanelPersistence GetPersistence() const override { return { "animation.facts" }; }
    DockSlot GetDockSlot() const override { return DockSlot::Right; }
    void OnDraw() override
    {
        if (!IsVisible()) return;
        ScopedPanel panel(GetTitle(), &Visible);
        if (!panel.IsOpen()) return;

        AnimationPreviewSession& session = Workspace.Simulation;
        const AnimBoundRig* rig = session.Rig();
        if (rig == nullptr || !rig->HasFacts)
        {
            ImGui::TextWrapped(rig == nullptr ? "Open a rig to drive its facts."
                                              : "This rig carries no facts: it is a Prop rig.");
            return;
        }
        if (!rig->Valid)
        {
            ImGui::TextWrapped("The rig does not bind; see Problems. Its entity gathers nothing.");
            return;
        }

        if (session.FactsExact())
            ImGui::TextWrapped("Derived facts are exact.");
        else
            ImGui::TextWrapped("Warming up: derived facts are exact after %.0f ms of observation.",
                               rig->HorizonMs);

        // While paused, a disposable evaluation of the next tick shows what the
        // scheduled edits will do; stepping commits it.
        const std::vector<std::uint32_t> next =
            session.IsPlaying() ? std::vector<std::uint32_t>{} : session.PreviewNextTick();
        const std::span<const std::uint32_t> facts = session.Facts();

        if (!ImGui::BeginTable("facts", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable
                                              | ImGuiTableFlags_BordersInnerV))
            return;
        ImGui::TableSetupColumn("Fact");
        ImGui::TableSetupColumn("Now");
        ImGui::TableSetupColumn("Next tick");
        ImGui::TableSetupColumn("Input or derivation");
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < rig->Slots.size() && i < facts.size(); ++i)
        {
            const AnimBoundFactSlot& slot = rig->Slots[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Selectable(slot.Name.c_str(), Selected == slot.Name, ImGuiSelectableFlags_SpanAllColumns
                                                                             | ImGuiSelectableFlags_AllowOverlap))
                Selected = slot.Name;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s%s\nDeclared in %s", AnimFactKindName(slot.Kind).data(),
                                  slot.Local ? ", local" : "", slot.DeclaredIn.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(FormatFact(session, slot.Kind, facts[i]).c_str());
            ImGui::TableNextColumn();
            if (i < next.size())
            {
                const bool changes = next[i] != facts[i];
                if (changes) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_PlotHistogram));
                ImGui::TextUnformatted(FormatFact(session, slot.Kind, next[i]).c_str());
                if (changes) ImGui::PopStyleColor();
            }
            ImGui::TableNextColumn();
            if (slot.Derivation >= 0)
                ImGui::TextDisabled("%s", DescribeDerivation(*rig, rig->Derivations[static_cast<std::size_t>(slot.Derivation)]).c_str());
            else
                DrawInput(session, slot, facts[i]);
            ImGui::PopID();
        }
        ImGui::EndTable();

        DrawHistory(session, *rig);
    }
private:
    void DrawInput(AnimationPreviewSession& session, const AnimBoundFactSlot& slot, std::uint32_t current)
    {
        const AnimationScenarioValue* input = session.Input(slot.Name);
        ImGui::SetNextItemWidth(-60.0f);
        switch (slot.Kind)
        {
        case AnimFactKind::Bool:
        {
            bool value = AnimFactToBool(current);
            if (ImGui::Checkbox("##bool", &value))
                session.SetFact(slot.Name, AnimationScenarioValue::FromBool(value));
            break;
        }
        case AnimFactKind::Float:
        case AnimFactKind::Int:
        {
            // A drag schedules one edit when it ends, not one per frame; until
            // then the field shows the dragged value, not the fact.
            const float fact = slot.Kind == AnimFactKind::Float ? AnimFactToFloat(current)
                                                                : static_cast<float>(AnimFactToInt(current));
            float value = Dragging == slot.Name ? Draft : fact;
            if (slot.Kind == AnimFactKind::Float)
                ImGui::DragFloat("##number", &value, 0.05f);
            else
            {
                int whole = static_cast<int>(value);
                if (ImGui::DragInt("##number", &whole)) value = static_cast<float>(whole);
            }
            if (ImGui::IsItemActivated())
                Dragging = slot.Name;
            if (Dragging == slot.Name)
                Draft = value;
            if (ImGui::IsItemDeactivatedAfterEdit())
                session.SetFact(slot.Name, AnimationScenarioValue::FromNumber(value));
            if (ImGui::IsItemDeactivated())
                Dragging.clear();
            break;
        }
        case AnimFactKind::Tag:
        {
            std::string& draft = TagDrafts[slot.Name];
            if (SubmitText("##tag", draft))
                session.SetFact(slot.Name, AnimationScenarioValue::FromName(draft));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("A gameplay tag name; Enter applies it.");
            break;
        }
        case AnimFactKind::TagSet:
            ImGui::TextDisabled("tag container; not driven by scenarios yet");
            return;
        }
        if (input != nullptr)
        {
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear")) session.ClearFact(slot.Name);
        }
        else
        {
            ImGui::SameLine();
            ImGui::TextDisabled("unset");
        }
    }

    void DrawHistory(const AnimationPreviewSession& session, const AnimBoundRig& rig)
    {
        const int slot = rig.FindSlot(Selected);
        if (slot < 0) return;
        const AnimFactKind kind = rig.Slots[static_cast<std::size_t>(slot)].Kind;
        std::vector<float> samples;
        for (const AnimationPreviewTickRecord& record : session.History())
        {
            if (static_cast<std::size_t>(slot) < record.Facts.size())
                samples.push_back(AnimFactToNumber(kind, record.Facts[static_cast<std::size_t>(slot)]));
        }
        ImGui::SeparatorText(std::format("{} over the last {} ticks", Selected, samples.size()).c_str());
        ImGui::PlotLines("##history", samples.data(), static_cast<int>(samples.size()), 0, nullptr,
                         FLT_MAX, FLT_MAX, ImVec2(-1.0f, 80.0f));
    }

    AnimationPreviewWorkspace& Workspace;
    std::string Selected;
    // The numeric fact being dragged, and its value until the drag ends.
    std::string Dragging;
    float Draft = 0.0f;
    std::unordered_map<std::string, std::string> TagDrafts;
};

class RequestsPanel final : public IEditorPanel
{
public:
    explicit RequestsPanel(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}
    std::string_view GetTitle() const override { return "Requests"; }
    PanelPersistence GetPersistence() const override { return { "animation.requests" }; }
    DockSlot GetDockSlot() const override { return DockSlot::RightBottom; }
    void OnDraw() override
    {
        if (!IsVisible()) return;
        ScopedPanel panel(GetTitle(), &Visible);
        if (!panel.IsOpen()) return;

        AnimationPreviewSession& session = Workspace.Simulation;
        const AnimBoundRig* rig = session.Rig();
        if (rig == nullptr)
        {
            ImGui::TextWrapped("Open a rig to issue requests against it.");
            return;
        }
        DrawIssueForm(session, *rig);
        ImGui::Separator();
        DrawLiveTable(session);
        DrawLastOutcomes(session);
    }
private:
    void DrawIssueForm(AnimationPreviewSession& session, const AnimBoundRig& rig)
    {
        const std::vector<std::string>& participants = session.Scenario().Participants;
        if (participants.empty())
        {
            ImGui::TextWrapped("Add a participant to issue requests from.");
            return;
        }
        Participant = std::min(Participant, participants.size() - 1);
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::BeginCombo("Source", participants[Participant].c_str()))
        {
            for (std::size_t i = 0; i < participants.size(); ++i)
                if (ImGui::Selectable(participants[i].c_str(), i == Participant)) Participant = i;
            ImGui::EndCombo();
        }

        const AnimBoundIntent* intent = nullptr;
        if (rig.HasRequestSchema)
        {
            if (rig.Intents.empty())
            {
                ImGui::TextWrapped("The rig's request schema declares no intents that resolve here.");
                return;
            }
            Intent = std::min(Intent, rig.Intents.size() - 1);
            intent = &rig.Intents[Intent];
            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::BeginCombo("Intent", intent->Name.c_str()))
            {
                for (std::size_t i = 0; i < rig.Intents.size(); ++i)
                    if (ImGui::Selectable(rig.Intents[i].Name.c_str(), i == Intent)) Intent = i;
                ImGui::EndCombo();
            }
            intent = &rig.Intents[Intent];
        }
        else
        {
            ImGui::SetNextItemWidth(220.0f);
            (void)SubmitText("Intent tag", FreeIntent);
        }

        ImGui::SetNextItemWidth(120.0f);
        ImGui::Combo("Lifetime", &Lifetime, kLifetimes.data(), static_cast<int>(kLifetimes.size()));
        if (Lifetime == static_cast<int>(AnimRequestLifetime::Fixed))
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100.0f);
            ImGui::InputInt("Ticks", &FixedTicks);
            FixedTicks = std::max(FixedTicks, 1);
        }
        ImGui::TextUnformatted("Layers");
        for (std::size_t i = 0; i < rig.Layers.size(); ++i)
        {
            ImGui::SameLine();
            bool on = (Layers >> i) & 1u;
            if (ImGui::Checkbox(rig.Layers[i].NameText.c_str(), &on))
                Layers = static_cast<std::uint8_t>(on ? Layers | (1u << i) : Layers & ~(1u << i));
        }

        std::vector<std::pair<std::string, AnimationScenarioValue>> params;
        if (intent != nullptr)
        {
            ParamDrafts.resize(intent->Params.size());
            for (std::size_t p = 0; p < intent->Params.size(); ++p)
            {
                const AnimBoundParam& param = intent->Params[p];
                ImGui::PushID(static_cast<int>(p));
                ImGui::SetNextItemWidth(160.0f);
                AnimationScenarioValue& draft = ParamDrafts[p];
                switch (param.Kind)
                {
                case AnimRequestParamKind::Float:
                {
                    float value = static_cast<float>(draft.Number);
                    if (ImGui::DragFloat(param.Name.c_str(), &value, 0.05f)) draft = AnimationScenarioValue::FromNumber(value);
                    break;
                }
                case AnimRequestParamKind::Int:
                {
                    int value = static_cast<int>(draft.Number);
                    if (ImGui::DragInt(param.Name.c_str(), &value)) draft = AnimationScenarioValue::FromNumber(value);
                    break;
                }
                case AnimRequestParamKind::Bool:
                {
                    bool value = draft.Bool;
                    if (ImGui::Checkbox(param.Name.c_str(), &value)) draft = AnimationScenarioValue::FromBool(value);
                    break;
                }
                case AnimRequestParamKind::Tag:
                {
                    std::string name = draft.Name;
                    (void)SubmitText(param.Name.c_str(), name);
                    draft = AnimationScenarioValue::FromName(name);
                    break;
                }
                }
                params.emplace_back(param.Name, draft);
                ImGui::PopID();
            }
        }

        if (ImGui::Button("Issue on next tick"))
        {
            AnimationScenarioAction action;
            action.Participant = participants[Participant];
            action.Intent = intent != nullptr ? intent->Name : FreeIntent;
            action.Lifetime = static_cast<AnimRequestLifetime>(Lifetime);
            action.FixedTicks = static_cast<std::uint32_t>(FixedTicks);
            action.Layers = Layers;
            action.Params = std::move(params);
            session.IssueRequest(std::move(action));
        }
    }

    void DrawLiveTable(AnimationPreviewSession& session)
    {
        const AnimRequestSet* set = session.Requests();
        if (set == nullptr) return;
        std::size_t occupied = 0;
        for (const AnimRequest& request : set->Records) occupied += request.Occupied ? 1 : 0;
        ImGui::Text("Records %zu / %zu (a full set refuses; nothing is evicted)", occupied, kAnimRequestCapacity);

        ImGui::SetNextItemWidth(140.0f);
        ImGui::Combo("Cancel reason", &Reason, kCancelReasons.data(), static_cast<int>(kCancelReasons.size()));
        if (!ImGui::BeginTable("requests", 8, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV
                                                  | ImGuiTableFlags_Resizable))
            return;
        for (const char* column : { "Seq", "Source", "Intent", "Lifetime", "Age", "Layers", "Status", "" })
            ImGui::TableSetupColumn(column);
        ImGui::TableHeadersRow();
        const AnimTick now = session.Tick();
        for (const AnimRequest& request : set->Records)
        {
            if (!request.Occupied) continue;
            ImGui::PushID(static_cast<int>(request.Id.Sequence));
            ImGui::TableNextRow();
            const std::string source(session.ParticipantName(request.Id.Source));
            const std::string intent = IntentName(session, request.Intent);
            const AnimRequest* primary = FindPrimaryAnimRequest(*set, request.Intent, now);
            ImGui::TableNextColumn(); ImGui::Text("%u", request.Id.Sequence);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(source.c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(intent.c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(AnimRequestLifetimeName(request.Lifetime).data());
            ImGui::TableNextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(now - request.StartTick));
            ImGui::TableNextColumn(); ImGui::Text("0x%02X", request.Layers);
            ImGui::TableNextColumn();
            if (request.IsCancelled())
                ImGui::Text("%s at %llu, kept to %llu", AnimCancelReasonName(request.CancelReason).data(),
                            static_cast<unsigned long long>(request.CancelTick),
                            static_cast<unsigned long long>(request.TailUntilTick));
            else if (!IsAnimRequestLive(request, now))
                ImGui::TextDisabled("expired");
            else
                ImGui::TextUnformatted(primary == &request ? "live, primary" : "live");
            ImGui::TableNextColumn();
            if (IsAnimRequestLive(request, now) && !source.empty() && ImGui::SmallButton("Cancel"))
                session.CancelRequest(source, intent, static_cast<AnimCancelReason>(Reason + 1));
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    void DrawLastOutcomes(const AnimationPreviewSession& session)
    {
        if (session.History().empty()) return;
        const AnimationPreviewTickRecord& record = session.History().back();
        for (const AnimationPreviewActionOutcome& outcome : record.Actions)
        {
            if (outcome.Kind != AnimationScenarioActionKind::IssueRequest
                && outcome.Kind != AnimationScenarioActionKind::CancelRequest)
                continue;
            if (!outcome.Problem.empty())
                ImGui::TextWrapped("Tick %llu: %s", static_cast<unsigned long long>(record.Tick), outcome.Problem.c_str());
            else if (outcome.Kind == AnimationScenarioActionKind::IssueRequest && !outcome.Request.Accepted())
                ImGui::TextWrapped("Tick %llu: %s refused (%s)", static_cast<unsigned long long>(record.Tick),
                                   outcome.Subject.c_str(), AnimRejectReasonName(outcome.Request.Reject).data());
            else if (outcome.Request.Status == AnimRequestStatus::Superseded)
                ImGui::TextWrapped("Tick %llu: %s superseded the source's previous request",
                                   static_cast<unsigned long long>(record.Tick), outcome.Subject.c_str());
            else if (outcome.Request.Status == AnimRequestStatus::Deduplicated)
                ImGui::TextWrapped("Tick %llu: %s was already issued this tick",
                                   static_cast<unsigned long long>(record.Tick), outcome.Subject.c_str());
        }
    }

    AnimationPreviewWorkspace& Workspace;
    std::size_t Participant = 0;
    std::size_t Intent = 0;
    std::string FreeIntent;
    int Lifetime = 0;
    int FixedTicks = 30;
    int Reason = 0;
    std::uint8_t Layers = kAnimAllLayers;
    std::vector<AnimationScenarioValue> ParamDrafts;
};

class SimulationTransportPanel final : public IEditorPanel
{
public:
    explicit SimulationTransportPanel(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}
    std::string_view GetTitle() const override { return "Simulation"; }
    PanelPersistence GetPersistence() const override { return { "animation.simulation" }; }
    DockSlot GetDockSlot() const override { return DockSlot::Bottom; }
    void OnDraw() override
    {
        if (!IsVisible()) return;
        ScopedPanel panel(GetTitle(), &Visible);
        if (!panel.IsOpen()) return;

        AnimationPreviewSession& session = Workspace.Simulation;
        if (!session.IsOpen())
        {
            ImGui::TextWrapped("Open a rig to simulate it under a scenario.");
            return;
        }
        if (ImGui::Button(session.IsPlaying() ? "Pause" : "Play"))
        {
            if (session.IsPlaying()) session.Pause(); else session.Play();
        }
        ImGui::SameLine();
        if (ImGui::Button("Restart scenario")) session.Restart();
        ImGui::SameLine();
        if (ImGui::Button("Next tick")) session.Step();
        ImGui::SameLine();
        float speed = static_cast<float>(session.Speed());
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::SliderFloat("Speed", &speed, 0.05f, 8.0f, "%.2fx")) (void)session.SetSpeed(speed);
        ImGui::SameLine();
        ImGui::Text("Tick %llu (%.3f s at %u Hz)", static_cast<unsigned long long>(session.Tick()),
                    static_cast<double>(session.Tick()) * session.TickSeconds(), session.Scenario().TickRate);

        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputInt("##target", &Target);
        Target = std::max(Target, 0);
        ImGui::SameLine();
        if (ImGui::Button("Run to tick"))
        {
            session.Pause();
            session.RunTo(static_cast<AnimTick>(Target));
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Going back replays the scenario from tick 0; acting there starts a new branch.");
    }
private:
    AnimationPreviewWorkspace& Workspace;
    int Target = 0;
};

class ProblemsChangesPanel final : public IEditorPanel
{
public:
    explicit ProblemsChangesPanel(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}
    std::string_view GetTitle() const override { return "Problems and changes"; }
    PanelPersistence GetPersistence() const override { return { "animation.problems" }; }
    DockSlot GetDockSlot() const override { return DockSlot::Bottom; }
    void OnDraw() override
    {
        if (!IsVisible()) return;
        ScopedPanel panel(GetTitle(), &Visible);
        if (!panel.IsOpen()) return;
        if (!ImGui::BeginTabBar("tabs")) return;
        if (ImGui::BeginTabItem("Problems"))
        {
            std::vector<AnimDiagnostic> problems = Workspace.Simulation.Problems();
            problems.insert(problems.end(), Workspace.ScenarioLoadProblems.begin(),
                            Workspace.ScenarioLoadProblems.end());
            if (problems.empty()) ImGui::TextDisabled("No problems with the open rig or scenario.");
            for (const AnimDiagnostic& problem : problems)
                ImGui::TextWrapped("%s %s", problem.Severity == AnimDiagnosticSeverity::Error ? "Error" : "Warning",
                                   FormatAnimDiagnostic(problem).c_str());
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Decision history"))
        {
            DrawDecisions();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Changes"))
        {
            DrawChanges();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
private:
    void DrawDecisions()
    {
        const AnimationPreviewSession& session = Workspace.Simulation;
        const AnimDecisionLog* log = session.DecisionLog();
        if (log == nullptr || log->Size() == 0)
        {
            ImGui::TextDisabled("Nothing has changed on the previewed entity yet.");
            return;
        }
        for (std::size_t i = log->Size(); i-- > 0;)
        {
            const AnimDecisionRecord& record = log->At(i);
            std::string text = std::format("{} {} #{} {}", record.Tick, AnimDecisionCauseName(record.Cause),
                                           record.Request.Sequence, IntentName(session, record.Intent));
            if (record.CancelReason != AnimCancelReason::None)
                text += std::format(" ({})", AnimCancelReasonName(record.CancelReason));
            if (record.RejectReason != AnimRejectReason::None)
                text += std::format(" ({})", AnimRejectReasonName(record.RejectReason));
            ImGui::TextUnformatted(text.c_str());
        }
    }

    void DrawChanges()
    {
        const AnimationPreviewSession& session = Workspace.Simulation;
        if (session.IsOpen())
            ImGui::TextWrapped("Scenario %s: %s", session.Scenario().Name.c_str(),
                               session.ScenarioModified() ? "recorded edits not saved" : "saved");
        bool any = false;
        for (const auto& document : Workspace.Documents)
        {
            if (!document->IsDirty()) continue;
            any = true;
            ImGui::BulletText("%s: unsaved edits", document->VirtualPath().c_str());
        }
        if (!any) ImGui::TextDisabled("No open document has unsaved edits.");
        ImGui::TextWrapped("Preview inputs, requests and transport never modify a document.");
    }

    AnimationPreviewWorkspace& Workspace;
};
}

void AddAnimationSimulationPanels(EditorUiFeature& ui, AnimationPreviewWorkspace& workspace)
{
    ui.AddPanel(std::make_unique<RigScenarioPanel>(workspace));
    ui.AddPanel(std::make_unique<FactsPanel>(workspace));
    ui.AddPanel(std::make_unique<RequestsPanel>(workspace));
    ui.AddPanel(std::make_unique<SimulationTransportPanel>(workspace));
    ui.AddPanel(std::make_unique<ProblemsChangesPanel>(workspace));
}
