#include "ui/AnimationEventPanels.h"

#include "authoring/AnimationEventBindings.h"
#include "authoring/AnimationPreviewWorkspace.h"
#include "ui/AnimationPreviewStatus.h"
#include "ui/DocumentSaveReportView.h"
#include "authoring/AnimationRigDocumentEdits.h"
#include "ui/WorkspaceView.h"
#include "ui/IEditorPanel.h"
#include "ui/ScopedPanel.h"

#include <anim/AnimRigData.h>
#include <anim/AnimationClipCache.h>
#include <assets/runtime/RuntimeAssets.h>
#include <authored/VerbBindingData.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{
// True on the frame editing finished.
bool EditText(const char* label, std::string& text)
{
    std::array<char, 256> buffer{};
    const std::size_t length = std::min(text.size(), buffer.size() - 1);
    std::memcpy(buffer.data(), text.data(), length);
    ImGui::InputText(label, buffer.data(), buffer.size());
    text = buffer.data();
    return ImGui::IsItemDeactivatedAfterEdit();
}

const char* KindName(DataFieldKind kind)
{
    switch (kind)
    {
    case DataFieldKind::Bool: return "bool";
    case DataFieldKind::Int: return "int";
    case DataFieldKind::Float: return "float";
    case DataFieldKind::String: return "string";
    case DataFieldKind::Enum: return "choice";
    case DataFieldKind::Vector: return "vector";
    case DataFieldKind::Record: return "record";
    case DataFieldKind::Array: return "array";
    case DataFieldKind::Optional: return "optional";
    case DataFieldKind::AssetRef: return "asset";
    case DataFieldKind::DataAssetRef: return "data asset";
    case DataFieldKind::GameplayTag: return "tag";
    case DataFieldKind::Entity: return "entity";
    }
    return "?";
}

VerbBindingArgument DefaultInput(const std::string& name, DataFieldKind kind)
{
    VerbBindingArgument input;
    input.Key = name;
    switch (kind)
    {
    case DataFieldKind::GameplayTag:
        input.Source = VerbArgumentSource::Tag;
        break;
    case DataFieldKind::Bool: input.Literal = JsonValue(false); break;
    case DataFieldKind::String:
    case DataFieldKind::Enum: input.Literal = JsonValue(std::string()); break;
    default: input.Literal = JsonValue(0.0); break;
    }
    return input;
}

std::string BindingFileOf(const AnimationPreviewWorkspace& workspace, const std::string& key)
{
    const DataAssetCache& data = workspace.DataCache();
    const AnimRigData* rig = data.TryGet<AnimRigData>(data.Find(workspace.Rig.Path), kAnimRigType);
    if (rig == nullptr)
        return {};
    for (const std::string& path : rig->BindingSetPaths)
        if (const VerbBindingLibrary* library = data.TryGet<VerbBindingLibrary>(data.Find(path), kVerbBindingsTypeName))
            if (library->Find(std::string_view(key)) != nullptr)
                return path;
    return {};
}

std::vector<std::string> RigBindingFiles(const AnimationPreviewWorkspace& workspace)
{
    const DataAssetCache& data = workspace.DataCache();
    const AnimRigData* rig = data.TryGet<AnimRigData>(data.Find(workspace.Rig.Path), kAnimRigType);
    return rig != nullptr ? rig->BindingSetPaths : std::vector<std::string>{};
}

class ClipEventsPanel final : public IEditorPanel
{
public:
    explicit ClipEventsPanel(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}
    std::string_view GetTitle() const override { return "Clip events"; }
    PanelPersistence GetPersistence() const override { return { "animation.clip_events" }; }
    DockSlot GetDockSlot() const override { return DockSlot::CenterBottom; }

    void OnDraw() override
    {
        if (!IsVisible())
            return;
        ScopedPanel panel(GetWindowName(), &Visible);
        if (!panel.IsOpen())
            return;

        DrawClipPicker();
        AnimationClipEventsDocument* document = Workspace.ClipEvents.Find(Workspace.ClipEvents.ActiveClip());
        if (document == nullptr)
        {
            ImGui::TextWrapped("Choose a clip cooked from a mesh source and open its events. They are authored "
                               "in the source's import sidecar and cooked into the clip.");
            return;
        }
        DrawToolbar(*document);
        DrawTrack(*document);
        ImGui::Separator();
        DrawInspector(*document);
    }

private:
    // The rig's clips, then the auditioned one.
    std::vector<std::string> CandidateClips() const
    {
        std::vector<std::string> clips;
        if (const AnimBoundRig* rig = Workspace.Rig.Simulation.Rig())
            for (const AnimBoundContent& content : rig->Contents)
                clips.push_back(content.Path);
        if (!Workspace.Audition.ClipPath.empty() && std::ranges::find(clips, Workspace.Audition.ClipPath) == clips.end())
            clips.push_back(Workspace.Audition.ClipPath);
        std::erase_if(clips, [](const std::string& clip) { return !MeshClipSourceOf(clip).has_value(); });
        return clips;
    }

    void DrawClipPicker()
    {
        const std::vector<std::string> clips = CandidateClips();
        const char* shown = Workspace.ClipEvents.ActiveClip().empty() ? "(choose a clip)" : Workspace.ClipEvents.ActiveClip().c_str();
        if (ImGui::BeginCombo("Clip", shown))
        {
            for (const std::string& clip : clips)
                if (ImGui::Selectable(clip.c_str(), clip == Workspace.ClipEvents.ActiveClip()))
                {
                    Workspace.Sources.CancelEdits();
                    if ((Workspace.ClipEvents.OpenOrFocus(clip, Workspace.DocumentError) != nullptr))
                        Selected.reset();
                }
            ImGui::EndCombo();
        }
        if (!Workspace.DocumentError.empty())
            ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.4f, 1.0f), "%s", Workspace.DocumentError.c_str());
    }

    void Changed(AnimationClipEventsDocument& document) { Workspace.ClipEvents.Changed(document); }

    void DrawToolbar(AnimationClipEventsDocument& document)
    {
        if (ImGui::Button("Add at playhead"))
            AddAt(document, Playhead(document).value_or(0.0f));
        ImGui::SameLine();
        ImGui::BeginDisabled(!Selected.has_value());
        if (ImGui::Button("Delete") && Selected)
        {
            document.Remove(*Selected);
            Selected.reset();
            Changed(document);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!Workspace.Sources.CanUndo());
        if (ImGui::Button("Undo"))
            Workspace.Sources.Undo();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!Workspace.Sources.CanRedo());
        if (ImGui::Button("Redo"))
            Workspace.Sources.Redo();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!document.IsDirty());
        if (ImGui::Button(document.IsDirty() ? "Save*" : "Save"))
            Workspace.DocumentError = DescribeDocumentSave(Workspace.Sources.Save(Workspace.ClipEvents.RefOf(document)));
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("%s", document.SidecarPath().filename().string().c_str());

        const std::string status = AnimationPreviewStatusText(Workspace.ClipEvents.PreviewStateOf(document));
        if (!status.empty())
            ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f), "%s", status.c_str());
        else
            ImGui::TextDisabled("The preview plays the working events. Only Save writes the sidecar.");
    }

    // Normalized; the simulation's playhead when a layer plays the clip, else the audition's.
    std::optional<float> Playhead(const AnimationClipEventsDocument& document) const
    {
        const AnimBoundRig* rig = Workspace.Rig.Simulation.Rig();
        const AnimContentState* content = Workspace.Rig.Simulation.Content();
        if (rig != nullptr && content != nullptr)
            for (std::size_t l = 0; l < rig->Layers.size() && l < kAnimMaxLayers; ++l)
            {
                const AnimLayerContent& layer = content->Layers[l];
                if (layer.Clip < rig->Contents.size() && rig->Contents[layer.Clip].Path == document.ClipPath()
                    && rig->Contents[layer.Clip].DurationSeconds > 0.0f)
                    return layer.TimeSeconds / rig->Contents[layer.Clip].DurationSeconds;
            }
        if (Workspace.Audition.ClipPath == document.ClipPath())
            return static_cast<float>(Workspace.Audition.Session.NormalizedTime());
        return std::nullopt;
    }

    void AddAt(AnimationClipEventsDocument& document, float time)
    {
        AnimationClipEvent event;
        event.Time = std::clamp(time, 0.0f, 1.0f);
        if (const AnimBoundRig* rig = Workspace.Rig.Simulation.Rig(); rig != nullptr && rig->Bindings.Size() > 0)
        {
            const CompiledVerbBinding& first = rig->Bindings.All().front();
            event.Binding = first.KeyText;
            for (const VerbCompiledInput& input : first.Inputs)
                event.Inputs.push_back(
                    DefaultInput(input.Name, input.Destinations.empty() ? DataFieldKind::Float
                                                                        : input.Destinations.front().Expected.Kind));
        }
        else
            event.Binding = "anim.event";
        Selected = document.Add(std::move(event));
        Changed(document);
    }

    void DrawTrack(AnimationClipEventsDocument& document)
    {
        const float width = std::max(ImGui::GetContentRegionAvail().x, 64.0f);
        const float height = 44.0f;
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##track", ImVec2(width, height));
        const bool hovered = ImGui::IsItemHovered();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), IM_COL32(28, 34, 38, 255));
        for (int tenth = 0; tenth <= 10; ++tenth)
        {
            const float x = origin.x + width * static_cast<float>(tenth) / 10.0f;
            draw->AddLine(ImVec2(x, origin.y + height - 6.0f), ImVec2(x, origin.y + height), IM_COL32(90, 100, 108, 255));
        }
        const auto timeAt = [&](float x) { return std::clamp((x - origin.x) / width, 0.0f, 1.0f); };
        const auto xAt = [&](float time) { return origin.x + time * width; };

        if (const std::optional<float> playhead = Playhead(document))
        {
            const float x = xAt(std::clamp(*playhead, 0.0f, 1.0f));
            draw->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + height), IM_COL32(230, 200, 90, 255), 2.0f);
        }

        // Scope is shown by shape (circle cosmetic, diamond gameplay), not color.
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        std::optional<std::uint32_t> under;
        for (const AnimationClipEvent& event : document.Events())
        {
            const ImVec2 at(xAt(event.Time), origin.y + height * 0.45f);
            const bool selected = Selected == event.Key;
            const ImU32 color = selected ? IM_COL32(120, 220, 210, 255)
                : event.Scope == AnimEventScope::Gameplay ? IM_COL32(230, 140, 90, 255)
                                                          : IM_COL32(170, 190, 200, 255);
            if (event.Scope == AnimEventScope::Gameplay)
                draw->AddQuadFilled(ImVec2(at.x, at.y - 7), ImVec2(at.x + 7, at.y), ImVec2(at.x, at.y + 7),
                                    ImVec2(at.x - 7, at.y), color);
            else
                draw->AddCircleFilled(at, 6.0f, color);
            const std::string label = event.Name.empty() ? std::format("{}", event.Key) : event.Name;
            draw->AddText(ImVec2(at.x + 8.0f, origin.y + 2.0f), IM_COL32(200, 205, 210, 255), label.c_str());
            if (std::abs(mouse.x - at.x) <= 7.0f && std::abs(mouse.y - at.y) <= 9.0f)
                under = event.Key;
        }

        // Escape and focus loss cancel the drag through the workspace.
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && under)
        {
            Selected = under;
            document.BeginEdit(*under);
        }
        if (document.IsEditing() && document.EditingKey() == Selected && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            if (const AnimationClipEvent* event = document.Find(*Selected))
            {
                AnimationClipEvent moved = *event;
                moved.Time = timeAt(mouse.x);
                document.PreviewEdit(std::move(moved));
                Changed(document);
            }
        }
        if (document.IsEditing() && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            document.CommitEdit();
            Changed(document);
        }
        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !under)
            AddAt(document, timeAt(mouse.x));
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && under)
        {
            document.Remove(*under);
            if (Selected == under)
                Selected.reset();
            Changed(document);
        }
        ImGui::TextDisabled("Double-click to add, drag to move, right-click to delete.");
    }

    void DrawInspector(AnimationClipEventsDocument& document)
    {
        const AnimationClipEvent* current = Selected ? document.Find(*Selected) : nullptr;
        if (current == nullptr)
        {
            ImGui::TextDisabled("Select an event.");
            Draft.reset();
            return;
        }
        // ImGui returns text only on frames it changed, so the fields edit a
        // persistent draft that resyncs with the document when nothing is active.
        if (!Draft || Draft->Key != current->Key
            || (DraftRevision != document.Revision() && !ImGui::IsAnyItemActive()))
        {
            Draft = *current;
            DraftRevision = document.Revision();
        }
        AnimationClipEvent& draft = *Draft;
        bool commit = false;

        ImGui::Text("Event %u", draft.Key);
        commit |= EditText("Name", draft.Name);

        float time = draft.Time;
        if (ImGui::DragFloat("Time", &time, 0.002f, 0.0f, 1.0f, "%.3f"))
        {
            document.BeginEdit(draft.Key);
            draft.Time = time;
            document.PreviewEdit(draft);
            Changed(document);
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            document.CommitEdit();
            Changed(document);
        }

        int scope = draft.Scope == AnimEventScope::Gameplay ? 1 : 0;
        bool scopeChanged = ImGui::RadioButton("Cosmetic", &scope, 0);
        ImGui::SameLine();
        scopeChanged = ImGui::RadioButton("Gameplay", &scope, 1) || scopeChanged;
        if (scopeChanged)
        {
            draft.Scope = scope == 1 ? AnimEventScope::Gameplay : AnimEventScope::Cosmetic;
            if (draft.Scope == AnimEventScope::Gameplay)
                draft.MinWeight.reset();
            commit = true;
        }
        if (draft.Scope == AnimEventScope::Cosmetic)
        {
            bool own = draft.MinWeight.has_value();
            if (ImGui::Checkbox("Own weight threshold", &own))
            {
                draft.MinWeight = own ? std::optional(0.5f) : std::nullopt;
                commit = true;
            }
            if (draft.MinWeight)
            {
                float weight = *draft.MinWeight;
                ImGui::SameLine();
                ImGui::SetNextItemWidth(120.0f);
                if (ImGui::SliderFloat("##weight", &weight, 0.0f, 1.0f, "%.2f"))
                    draft.MinWeight = weight;
                commit |= ImGui::IsItemDeactivatedAfterEdit();
            }
            else
                ImGui::TextDisabled("Below the behavior's event weight, the event is suppressed.");
        }
        else
            ImGui::TextDisabled("Produced only by the simulation authority; a client never produces it.");

        commit |= DrawBinding(document, draft);
        DrawInputs(draft, commit);

        if (commit)
        {
            document.Replace(draft);
            Changed(document);
            DraftRevision = document.Revision();
        }
        for (const std::string& problem : document.Problems())
            ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.4f, 1.0f), "%s", problem.c_str());
    }

    // True when the draft changed.
    bool DrawBinding(AnimationClipEventsDocument& document, AnimationClipEvent& draft)
    {
        (void)document;
        const AnimBoundRig* rig = Workspace.Rig.Simulation.Rig();
        const VerbRegistry* verbs = Workspace.Rig.Simulation.Verbs();
        bool changed = false;
        if (ImGui::BeginCombo("Binding", draft.Binding.c_str()))
        {
            EditText("Search", BindingFilter);
            if (rig != nullptr && verbs != nullptr)
                for (const AnimationBindingView& view : DescribeAnimationBindings(*rig, *verbs))
                {
                    const std::string label = std::format("{}  ->  {}", view.Key, view.Verb);
                    if (!BindingFilter.empty() && label.find(BindingFilter) == std::string::npos)
                        continue;
                    if (ImGui::Selectable(label.c_str(), view.Key == draft.Binding))
                    {
                        draft.Binding = view.Key;
                        // Keep what already fits; start the rest from defaults.
                        std::vector<VerbBindingArgument> inputs;
                        for (const AnimationBindingInputView& input : view.Inputs)
                        {
                            const auto kept = std::ranges::find(draft.Inputs, input.Name, &VerbBindingArgument::Key);
                            inputs.push_back(kept != draft.Inputs.end()
                                                 ? *kept
                                                 : DefaultInput(input.Name, input.Destinations.empty()
                                                                                ? DataFieldKind::Float
                                                                                : input.Destinations.front().second));
                        }
                        draft.Inputs = std::move(inputs);
                        changed = true;
                    }
                }
            ImGui::EndCombo();
        }

        const std::string file = BindingFileOf(Workspace, draft.Binding);
        ImGui::BeginDisabled(file.empty());
        if (ImGui::Button("Open binding"))
            (void)(void)Workspace.Documents.OpenOrFocus(file, Workspace.DocumentError);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Create binding..."))
            ImGui::OpenPopup("create_binding");
        if (ImGui::BeginPopup("create_binding"))
        {
            const std::vector<std::string> files = RigBindingFiles(Workspace);
            if (NewBindingFile.empty() && !files.empty())
                NewBindingFile = files.front();
            if (ImGui::BeginCombo("In", NewBindingFile.c_str()))
            {
                for (const std::string& path : files)
                    if (ImGui::Selectable(path.c_str(), path == NewBindingFile))
                        NewBindingFile = path;
                ImGui::EndCombo();
            }
            if (ImGui::BeginCombo("Verb", NewBindingVerb.c_str()) && verbs != nullptr)
            {
                for (const VerbId id : verbs->Live())
                    if (const VerbDefinition* definition = verbs->Get(id))
                        if (ImGui::Selectable(definition->Name.c_str(), definition->Name == NewBindingVerb))
                            NewBindingVerb = definition->Name;
                ImGui::EndCombo();
            }
            EditText("Key", NewBindingKey);
            ImGui::BeginDisabled(NewBindingFile.empty() || NewBindingVerb.empty() || NewBindingKey.empty());
            if (ImGui::Button("Create"))
            {
                if (CreateAnimationBinding(Workspace.Documents, Workspace.Rig.Simulation, NewBindingFile, NewBindingKey, NewBindingVerb, Workspace.DocumentError))
                {
                    Workspace.Rig.Simulation.Rebind();
                    draft.Binding = NewBindingKey;
                    draft.Inputs.clear();
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndDisabled();
            if (files.empty())
                ImGui::TextDisabled("The rig lists no bindings files.");
            ImGui::EndPopup();
        }

        const CompiledVerbBinding* binding = rig != nullptr ? rig->Bindings.Find(draft.Binding) : nullptr;
        const VerbDefinition* definition = binding != nullptr && verbs != nullptr ? verbs->Get(binding->Verb) : nullptr;
        if (definition != nullptr && ImGui::TreeNode("contract", "Contract: %s", definition->Name.c_str()))
        {
            for (const DataFieldSchema& argument : definition->Arguments.Children)
                ImGui::BulletText("%s : %s", argument.Key.c_str(), KindName(argument.Kind));
            ImGui::TreePop();
        }
        else if (binding == nullptr)
            ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.4f, 1.0f), "No binding '%s' in the rig's bindings.",
                               draft.Binding.c_str());
        return changed;
    }

    void DrawInputs(AnimationClipEvent& draft, bool& commit)
    {
        const AnimBoundRig* rig = Workspace.Rig.Simulation.Rig();
        const CompiledVerbBinding* binding = rig != nullptr ? rig->Bindings.Find(draft.Binding) : nullptr;
        if (binding == nullptr)
            return;
        const VerbBindingEnvironment environment{ .Verbs = Workspace.Rig.Simulation.Verbs(),
                                                  .Tags = Workspace.Rig.Simulation.Tags() };
        const std::vector<AnimationEventInputCheck> checks = CheckAnimationEventInputs(draft, *binding, environment);
        const VerbRegistry* verbs = Workspace.Rig.Simulation.Verbs();
        const VerbDefinition* definition = verbs != nullptr ? verbs->Get(binding->Verb) : nullptr;

        for (const VerbCompiledInput& input : binding->Inputs)
        {
            ImGui::PushID(input.Name.c_str());
            const DataFieldKind kind = input.Destinations.empty() ? DataFieldKind::Float
                                                                  : input.Destinations.front().Expected.Kind;
            auto it = std::ranges::find(draft.Inputs, input.Name, &VerbBindingArgument::Key);
            if (it == draft.Inputs.end())
            {
                if (ImGui::Button(std::format("Supply {}", input.Name).c_str()))
                {
                    draft.Inputs.push_back(DefaultInput(input.Name, kind));
                    commit = true;
                }
                ImGui::PopID();
                continue;
            }
            commit |= DrawValue(input.Name, kind, *it);

            std::string destinations;
            for (const VerbInputDestination& destination : input.Destinations)
            {
                const std::string argument = definition != nullptr
                        && destination.ArgumentSlot < definition->Arguments.Children.size()
                    ? definition->Arguments.Children[destination.ArgumentSlot].Key
                    : std::string("?");
                destinations += std::format("{}{} ({})", destinations.empty() ? "" : ", ", argument,
                                            KindName(destination.Expected.Kind));
            }
            ImGui::SameLine();
            ImGui::TextDisabled("-> %s", destinations.c_str());
            const auto check = std::ranges::find(checks, input.Name, &AnimationEventInputCheck::Input);
            if (check != checks.end() && !check->Valid)
                ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.4f, 1.0f), "  %s", check->Message.c_str());
            ImGui::PopID();
        }
        for (const AnimationEventInputCheck& check : checks)
            if (!check.Valid && check.Supplied
                && std::ranges::none_of(binding->Inputs, [&](const VerbCompiledInput& in) { return in.Name == check.Input; }))
            {
                ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.4f, 1.0f), "%s: %s", check.Input.c_str(),
                                   check.Message.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton(std::format("Remove##{}", check.Input).c_str()))
                {
                    std::erase_if(draft.Inputs, [&](const VerbBindingArgument& value) { return value.Key == check.Input; });
                    commit = true;
                }
            }
    }

    // True when the value was committed.
    bool DrawValue(const std::string& name, DataFieldKind kind, VerbBindingArgument& value)
    {
        if (kind == DataFieldKind::GameplayTag)
        {
            value.Source = VerbArgumentSource::Tag;
            bool changed = false;
            if (ImGui::BeginCombo(name.c_str(), value.Text.empty() ? "(choose a tag)" : value.Text.c_str()))
            {
                if (const GameplayTagRegistry* tags = Workspace.Rig.Simulation.Tags())
                    for (std::size_t id = 1; id <= tags->Size(); ++id)
                    {
                        const std::string_view tag = tags->GetName(GameplayTagId{ static_cast<std::uint32_t>(id) });
                        if (!tag.empty() && ImGui::Selectable(std::string(tag).c_str(), tag == value.Text))
                        {
                            value.Text = std::string(tag);
                            changed = true;
                        }
                    }
                ImGui::EndCombo();
            }
            return changed;
        }
        value.Source = VerbArgumentSource::Literal;
        switch (kind)
        {
        case DataFieldKind::Bool:
        {
            bool flag = value.Literal.IsBool() && value.Literal.AsBool();
            if (ImGui::Checkbox(name.c_str(), &flag))
            {
                value.Literal = JsonValue(flag);
                return true;
            }
            return false;
        }
        case DataFieldKind::Int:
        case DataFieldKind::Float:
        {
            double number = value.Literal.IsNumber() ? value.Literal.AsNumber() : 0.0;
            if (ImGui::InputDouble(name.c_str(), &number, 0.0, 0.0, kind == DataFieldKind::Int ? "%.0f" : "%.3f"))
                value.Literal = JsonValue(number);
            return ImGui::IsItemDeactivatedAfterEdit();
        }
        case DataFieldKind::String:
        case DataFieldKind::Enum:
        {
            std::string text = value.Literal.IsString() ? value.Literal.AsString() : std::string();
            const bool done = EditText(name.c_str(), text);
            value.Literal = JsonValue(text);
            return done;
        }
        default:
        {
            // Vectors, records and arrays edit as their JSON text.
            std::string text = JsonStringify(value.Literal);
            const bool done = EditText(name.c_str(), text);
            if (done)
                if (std::optional<JsonValue> parsed = JsonParse(text))
                    value.Literal = std::move(*parsed);
            return done;
        }
        }
    }

    AnimationPreviewWorkspace& Workspace;
    std::optional<std::uint32_t> Selected;
    std::optional<AnimationClipEvent> Draft;
    std::uint64_t DraftRevision = 0;
    std::string BindingFilter;
    std::string NewBindingFile;
    std::string NewBindingVerb;
    std::string NewBindingKey;
};

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
    auto events = std::make_unique<ClipEventsPanel>(workspace);
    auto* eventsPanel = events.get();
    ui.AddPanel(std::move(events));
    ui.AddPanel(std::make_unique<EventAdmissionsPanel>(workspace));
    // A hidden panel is not drawn, so a drag it started cannot finish there.
    ui.Overlays.push_back([&workspace, eventsPanel] {
        if (!eventsPanel->IsVisible())
            for (const auto& document : workspace.ClipEvents.Documents())
                workspace.ClipEvents.CancelEdit(*document);
    });
}
