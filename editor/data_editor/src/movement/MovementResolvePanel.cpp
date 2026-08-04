#include "MovementResolvePanel.h"

#include "MovementLayerSummary.h"
#include "MovementProfileForm.h"
#include "MovementResolvePreview.h"

#include "../DataDocument.h"
#include "../DataEditorWorkspace.h"

#include "ui/EditorUiStyle.h"
#include "ui/ScopedPanel.h"

#include <imgui.h>

#include <algorithm>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace
{
    struct SupportChoice
    {
        SupportKind Kind;
        const char* Label;
    };

    constexpr SupportChoice kSupportChoices[] = {
        { SupportKind::Stable, "On stable ground" },
        { SupportKind::None, "In the air" },
        { SupportKind::Steep, "On steep ground" },
    };

    const char* SupportLabel(SupportKind kind)
    {
        for (const SupportChoice& choice : kSupportChoices)
        {
            if (choice.Kind == kind)
                return choice.Label;
        }
        return "Unknown";
    }

    bool PatchTouches(const MovementTuningPatch& patch, const MovementTuningField& field)
    {
        return (patch.*field.Patch).has_value();
    }

    // The rule that last moved this coefficient, which is the answer to "why is
    // it that value?". Walks matched layers in application order and reports the
    // index so the caller can name it the same way the rule list does.
    std::optional<std::size_t> FindWriter(const BoundMovementProfile& profile,
                                          std::span<const MovementLayerTrace> trace,
                                          const MovementTuningField& field)
    {
        std::optional<std::size_t> writer;
        const std::size_t shared = std::min(profile.Layers.size(), trace.size());
        for (std::size_t index = 0; index < shared; ++index)
        {
            if (!trace[index].Matched)
                continue;
            const BoundMovementLayer& layer = profile.Layers[index];
            if (PatchTouches(layer.Set, field) || PatchTouches(layer.Scale, field)
                || PatchTouches(layer.Add, field))
            {
                writer = index;
            }
        }
        return writer;
    }

    // Trace entries are the shared layers in authored order, then the active
    // mode's. Only the shared ones line up with the compiled profile here, which
    // is what lets an unnamed rule be described by its condition instead of by
    // its JSON path.
    std::string TraceName(const MovementResolvePreview& preview,
                          std::span<const MovementLayerTrace> trace,
                          std::size_t index)
    {
        if (index >= trace.size())
            return {};
        if (!trace[index].Name.empty())
            return trace[index].Name;
        if (const CompiledMovementProfile* compiled = preview.Compiled())
        {
            if (index < compiled->Layers.size())
                return SynthesizeMovementLayerName(compiled->Layers[index].When);
        }
        return trace[index].SourcePath;
    }
}

MovementResolvePanel::MovementResolvePanel(DataEditorWorkspace& workspace,
                                           MovementResolvePreview& preview)
    : Workspace(workspace)
    , Preview(preview)
{
}

void MovementResolvePanel::OnDraw()
{
    ScopedPanel panel(GetTitle(), &Visible);
    if (!panel.IsOpen())
        return;

    const DataDocument* document = Workspace.Active();
    if (document == nullptr || document->Subtype() != MovementProfileSubtype())
    {
        ImGui::TextDisabled("Open a movement profile to resolve it.");
        return;
    }

    if (!Preview.IsBound())
    {
        ImGui::TextColored(EditorUi::Warning, "%s",
                           Preview.Error().empty() ? "The profile does not compile."
                                                   : Preview.Error().c_str());
        return;
    }

    MovementPreviewContext& context = Preview.Context();

    ImGui::SeparatorText("Pretend the character is");
    if (ImGui::BeginCombo("Support", SupportLabel(context.Support)))
    {
        for (const SupportChoice& choice : kSupportChoices)
        {
            if (ImGui::Selectable(choice.Label, choice.Kind == context.Support))
                context.Support = choice.Kind;
        }
        ImGui::EndCombo();
    }

    float immersion = context.Immersion * 100.0f;
    if (ImGui::DragFloat("Immersion", &immersion, 1.0f, 0.0f, 100.0f, "%.0f%%"))
        context.Immersion = std::clamp(immersion, 0.0f, 100.0f) / 100.0f;

    ImGui::DragFloat("Move speed attribute (m/s)", &context.MoveSpeed, 0.05f, 0.0f, 100.0f, "%.2f");
    ImGui::SetItemTooltip(
        "Seeds Maximum speed before any rule runs. In game this comes from the "
        "MoveSpeed attribute, so effects can move it.");

    const std::vector<std::string>& knownModes = Preview.KnownModes();
    const std::string modeLabel = context.Mode.empty() ? "movement.mode.free" : context.Mode;
    if (ImGui::BeginCombo("Mode", modeLabel.c_str()))
    {
        for (const std::string& mode : knownModes)
        {
            if (ImGui::Selectable(mode.c_str(), mode == context.Mode))
                context.Mode = mode;
        }
        ImGui::EndCombo();
    }

    if (!Preview.KnownTags().empty())
    {
        ImGui::TextDisabled("Tags held");
        for (const std::string& tag : Preview.KnownTags())
        {
            const auto found = std::find(context.ActiveTags.begin(), context.ActiveTags.end(), tag);
            bool held = found != context.ActiveTags.end();
            if (ImGui::Checkbox(tag.c_str(), &held))
            {
                if (held)
                    context.ActiveTags.push_back(tag);
                else
                    context.ActiveTags.erase(found);
            }
        }
    }

    const BoundMovementProfile& profile = *Preview.BoundProfile();
    const MovementResolveResult resolved = Preview.Resolve();

    ImGui::SeparatorText("Rules");
    for (std::size_t index = 0; index < resolved.Trace.size(); ++index)
    {
        const MovementLayerTrace& entry = resolved.Trace[index];
        const std::string name = TraceName(Preview, resolved.Trace, index);
        if (entry.Matched)
        {
            ImGui::TextColored(EditorUi::Success, "active");
        }
        else
        {
            ImGui::TextDisabled("off");
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(name.c_str());
        if (!entry.Matched && !entry.Failure.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", entry.Failure.c_str());
        }
    }

    ImGui::SeparatorText("Resulting coefficients");
    // Same display names and units the form uses, read from the registered
    // schema rather than restated here.
    const std::vector<MovementCoefficientLabel> labels =
        Workspace.ActiveSchema() ? MovementCoefficientLabels(*Workspace.ActiveSchema())
                                 : std::vector<MovementCoefficientLabel>{};
    const auto labelFor = [&labels](std::string_view key) -> std::string
    {
        const auto found = std::find_if(labels.begin(), labels.end(),
            [key](const MovementCoefficientLabel& entry) { return entry.Key == key; });
        if (found == labels.end())
            return std::string(key);
        return found->Units.empty() ? found->Display
                                    : found->Display + " (" + found->Units + ")";
    };

    if (ImGui::BeginTable("effective", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("Coefficient");
        ImGui::TableSetupColumn("Value");
        ImGui::TableSetupColumn("Set by");
        ImGui::TableHeadersRow();

        const ResolvedMovementTuning defaults;
        for (const MovementTuningField& field : MovementTuningFields())
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(labelFor(field.Key).c_str());

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(
                FormatMovementCoefficient(resolved.Tuning.*field.Resolved).c_str());

            ImGui::TableNextColumn();
            if (const auto writer = FindWriter(profile, resolved.Trace, field))
            {
                ImGui::TextUnformatted(TraceName(Preview, resolved.Trace, *writer).c_str());
            }
            else if (field.Key == "max_speed")
            {
                ImGui::TextDisabled("MoveSpeed attribute");
            }
            else
            {
                ImGui::TextDisabled("engine default (%s)",
                    FormatMovementCoefficient(defaults.*field.Resolved).c_str());
            }
        }
        ImGui::EndTable();
    }
}
