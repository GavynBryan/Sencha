#include "MovementProfileForm.h"

#include "MovementLayerSummary.h"
#include "MovementResolvePreview.h"

#include "../DataEditorWorkspace.h"

#include "ui/ButtonFlow.h"
#include "ui/EditorUiStyle.h"

#include <core/metadata/DataSchema.h>
#include <movement/MovementProfileRuntime.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace
{
    constexpr std::string_view kSubtype = "movement.profile";

    // The three ways a layer can touch a coefficient, in the order the runtime
    // applies them. Designer-facing verbs; the JSON keys stay mechanical.
    struct PatchOperation
    {
        const char* Key;
        const char* Verb;
        const char* Summary;
    };

    constexpr std::array<PatchOperation, 3> kOperations = { {
        { "set", "Replace", "Replace the value outright." },
        { "scale", "Multiply", "Multiply what earlier layers produced." },
        { "add", "Offset", "Add to what earlier layers produced." },
    } };

    JsonValue* FindOrNull(JsonValue& object, std::string_view key)
    {
        return object.IsObject() ? object.Find(key) : nullptr;
    }

    JsonValue& EnsureMember(JsonValue& parent, std::string_view key, JsonValue fallback)
    {
        if (JsonValue* existing = parent.Find(key))
            return *existing;
        parent.AsObject().emplace_back(std::string(key), std::move(fallback));
        return parent.AsObject().back().second;
    }

    JsonValue& EnsureObject(JsonValue& parent, std::string_view key)
    {
        return EnsureMember(parent, key, JsonValue(JsonValue::Object{}));
    }

    void SetMember(JsonValue& parent, std::string_view key, JsonValue value)
    {
        if (JsonValue* existing = parent.Find(key))
            *existing = std::move(value);
        else
            parent.AsObject().emplace_back(std::string(key), std::move(value));
    }

    void EraseKey(JsonValue& object, std::string_view key)
    {
        if (!object.IsObject())
            return;
        auto& members = object.AsObject();
        members.erase(std::remove_if(members.begin(), members.end(),
            [key](const auto& item) { return item.first == key; }), members.end());
    }

    void DimText(std::string_view text)
    {
        ImGui::TextDisabled("%.*s", static_cast<int>(text.size()), text.data());
    }

    // A drag row for one coefficient, labelled and carrying its unit.
    FieldEdit DrawCoefficientRow(JsonValue& patch,
                                 const MovementCoefficientLabel& label,
                                 const DataFieldSchema* fieldSchema,
                                 const std::string& path,
                                 DataEditorWorkspace& workspace,
                                 bool& removed)
    {
        FieldEdit edit;
        JsonValue* value = FindOrNull(patch, label.Key);
        if (value == nullptr)
            return edit;

        ImGui::PushID(label.Key.c_str());
        double current = value->IsNumber() ? value->AsNumber() : 0.0;
        const double step = fieldSchema && fieldSchema->Numeric.Step
            ? *fieldSchema->Numeric.Step : 0.05;
        const double* minimum = fieldSchema && fieldSchema->Numeric.Minimum
            ? &*fieldSchema->Numeric.Minimum : nullptr;
        const double* maximum = fieldSchema && fieldSchema->Numeric.Maximum
            ? &*fieldSchema->Numeric.Maximum : nullptr;

        const std::string text = label.Units.empty()
            ? label.Display : std::format("{} ({})", label.Display, label.Units);

        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
        const bool dragged = ImGui::DragScalar(text.c_str(), ImGuiDataType_Double, &current,
                                               static_cast<float>(step), minimum, maximum, "%.3f");
        if (dragged)
            *value = JsonValue(current);
        edit.Changed = dragged;
        edit.Committed = ImGui::IsItemDeactivatedAfterEdit();

        if (fieldSchema != nullptr)
            DrawDataFieldHelp(workspace, *fieldSchema, path);

        ImGui::SameLine();
        if (ImGui::SmallButton("Clear"))
            removed = true;
        ImGui::PopID();
        return edit;
    }

    // Offers only the coefficients this operation does not already carry.
    FieldEdit DrawAddCoefficient(JsonValue& layer,
                                 const PatchOperation& operation,
                                 const std::vector<MovementCoefficientLabel>& labels)
    {
        FieldEdit edit;
        const std::string popup = std::format("add_{}", operation.Key);
        if (ImGui::Button(operation.Verb))
            ImGui::OpenPopup(popup.c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", operation.Summary);

        if (!ImGui::BeginPopup(popup.c_str()))
            return edit;

        const JsonValue* patch = FindOrNull(layer, operation.Key);
        bool anyOffered = false;
        for (const MovementCoefficientLabel& label : labels)
        {
            if (patch != nullptr && patch->Find(label.Key) != nullptr)
                continue;
            anyOffered = true;
            if (ImGui::Selectable(label.Display.c_str()))
            {
                JsonValue& target = EnsureObject(layer, operation.Key);
                // Multiplying starts at the identity so adding the row alone
                // cannot change behaviour; the other two start neutral at zero.
                const double initial = std::strcmp(operation.Key, "scale") == 0 ? 1.0 : 0.0;
                target.AsObject().emplace_back(label.Key, JsonValue(initial));
                edit |= FieldEdit::Instant();
            }
        }
        if (!anyOffered)
            DimText("Every coefficient is already set here.");
        ImGui::EndPopup();
        return edit;
    }

    FieldEdit DrawTagList(JsonValue& tags,
                          const char* key,
                          const char* label,
                          const std::vector<std::string>& known)
    {
        FieldEdit edit;
        JsonValue* list = FindOrNull(tags, key);
        const bool present = list != nullptr && list->IsArray() && !list->AsArray().empty();

        ImGui::PushID(key);
        if (present)
        {
            DimText(label);
            JsonValue::Array& values = list->AsArray();
            std::optional<std::size_t> remove;
            for (std::size_t index = 0; index < values.size(); ++index)
            {
                ImGui::PushID(static_cast<int>(index));
                std::array<char, 256> buffer{};
                if (values[index].IsString())
                {
                    const std::string& text = values[index].AsString();
                    std::memcpy(buffer.data(), text.c_str(),
                                std::min(text.size(), buffer.size() - 1));
                }
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
                const bool edited = ImGui::InputText("##tag", buffer.data(), buffer.size());
                if (edited)
                    values[index] = JsonValue(std::string(buffer.data()));
                edit.Changed = edit.Changed || edited;
                edit.Committed = edit.Committed || ImGui::IsItemDeactivatedAfterEdit();
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove"))
                    remove = index;
                ImGui::PopID();
            }
            if (remove)
            {
                values.erase(values.begin() + static_cast<std::ptrdiff_t>(*remove));
                if (values.empty())
                    EraseKey(tags, key);
                edit |= FieldEdit::Instant();
            }
        }

        const std::string popup = std::format("pick_{}", key);
        if (ImGui::SmallButton(present ? "Add" : label))
            ImGui::OpenPopup(popup.c_str());
        if (ImGui::BeginPopup(popup.c_str()))
        {
            for (const std::string& tag : known)
            {
                if (!ImGui::Selectable(tag.c_str()))
                    continue;
                if (list == nullptr || !list->IsArray())
                {
                    tags.AsObject().emplace_back(key, JsonValue(JsonValue::Array{}));
                    list = tags.Find(key);
                }
                list->AsArray().emplace_back(tag);
                edit |= FieldEdit::Instant();
            }
            if (known.empty())
                DimText("No tags are known here.");
            ImGui::EndPopup();
        }
        ImGui::PopID();
        return edit;
    }

    FieldEdit DrawCondition(JsonValue& layer,
                            const DataFieldSchema& layerSchema,
                            const MovementResolvePreview& preview)
    {
        FieldEdit edit;
        const DataFieldSchema* whenSchema = FindChild(layerSchema, "when");
        if (whenSchema == nullptr)
            return edit;

        JsonValue& when = EnsureObject(layer, "when");
        if (!when.IsObject())
            return edit;

        // Support: the enum's own display names carry the designer wording.
        if (const DataFieldSchema* supportSchema = FindChild(*whenSchema, "support"))
        {
            const JsonValue* value = when.Find("support");
            const std::string current = value && value->IsString() ? value->AsString() : "any";
            const auto choice = std::find_if(
                supportSchema->EnumChoices.begin(), supportSchema->EnumChoices.end(),
                [&current](const DataEnumChoice& item) { return item.Value == current; });
            const char* preview_ = choice == supportSchema->EnumChoices.end()
                ? current.c_str() : choice->DisplayName.c_str();

            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
            if (ImGui::BeginCombo("Support", preview_))
            {
                for (const DataEnumChoice& option : supportSchema->EnumChoices)
                {
                    if (ImGui::Selectable(option.DisplayName.c_str(), option.Value == current))
                    {
                        if (option.Value == "any")
                            EraseKey(when, "support");
                        else
                            SetMember(when, "support", JsonValue(option.Value));
                        edit |= FieldEdit::Instant();
                    }
                    if (ImGui::IsItemHovered() && !option.Description.empty())
                        ImGui::SetTooltip("%s", option.Description.c_str());
                }
                ImGui::EndCombo();
            }
        }

        // Mode, offered only once the author asks for it: most layers are
        // mode-agnostic and an always-visible empty box reads as a missing value.
        if (JsonValue* mode = when.Find("mode"))
        {
            std::array<char, 256> buffer{};
            if (mode->IsString())
            {
                const std::string& text = mode->AsString();
                std::memcpy(buffer.data(), text.c_str(), std::min(text.size(), buffer.size() - 1));
            }
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
            const bool edited = ImGui::InputText("Mode", buffer.data(), buffer.size());
            if (edited)
                *mode = JsonValue(std::string(buffer.data()));
            edit.Changed = edit.Changed || edited;
            edit.Committed = edit.Committed || ImGui::IsItemDeactivatedAfterEdit();
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear##mode"))
            {
                EraseKey(when, "mode");
                edit |= FieldEdit::Instant();
            }
            if (ImGui::BeginPopupContextItem("mode_suggestions"))
            {
                for (const std::string& known : preview.KnownModes())
                {
                    if (ImGui::Selectable(known.c_str()))
                    {
                        SetMember(when, "mode", JsonValue(known));
                        edit |= FieldEdit::Instant();
                    }
                }
                ImGui::EndPopup();
            }
        }

        if (JsonValue* immersion = when.Find("immersion_at_least"))
        {
            float percent = immersion->IsNumber()
                ? static_cast<float>(immersion->AsNumber()) * 100.0f : 0.0f;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
            const bool dragged = ImGui::DragFloat("Immersion at least", &percent,
                                                  1.0f, 0.0f, 100.0f, "%.0f%%");
            if (dragged)
                *immersion = JsonValue(static_cast<double>(std::clamp(percent, 0.0f, 100.0f)) / 100.0);
            edit.Changed = edit.Changed || dragged;
            edit.Committed = edit.Committed || ImGui::IsItemDeactivatedAfterEdit();
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear##immersion"))
            {
                EraseKey(when, "immersion_at_least");
                edit |= FieldEdit::Instant();
            }
        }

        JsonValue& tags = EnsureObject(when, "tags");
        if (tags.IsObject())
        {
            edit |= DrawTagList(tags, "all", "Requires tags", preview.KnownTags());
            edit |= DrawTagList(tags, "any", "Requires any of", preview.KnownTags());
            edit |= DrawTagList(tags, "none", "Blocked by tags", preview.KnownTags());
        }

        ButtonFlow more;
        if (when.Find("mode") == nullptr && more.Button("Add mode condition"))
        {
            when.AsObject().emplace_back("mode", JsonValue(std::string{}));
            edit |= FieldEdit::Instant();
        }
        if (when.Find("immersion_at_least") == nullptr && more.Button("Add immersion condition"))
        {
            when.AsObject().emplace_back("immersion_at_least", JsonValue(0.5));
            edit |= FieldEdit::Instant();
        }

        // An empty tag record or condition record would serialize as clutter the
        // author never asked for.
        if (tags.IsObject() && tags.AsObject().empty())
            EraseKey(when, "tags");
        if (when.IsObject() && when.AsObject().empty())
            EraseKey(layer, "when");
        return edit;
    }

    FieldEdit DrawLayerBody(JsonValue& layer,
                            const DataFieldSchema& layerSchema,
                            const std::vector<MovementCoefficientLabel>& labels,
                            const MovementResolvePreview& preview,
                            const std::string& layerPath,
                            DataEditorWorkspace& workspace)
    {
        FieldEdit edit;

        // Name first: it is what the collapsed card is read by.
        std::array<char, 256> nameBuffer{};
        if (const JsonValue* name = FindOrNull(layer, "name"); name && name->IsString())
        {
            const std::string& text = name->AsString();
            std::memcpy(nameBuffer.data(), text.c_str(),
                        std::min(text.size(), nameBuffer.size() - 1));
        }
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
        const bool nameEdited = ImGui::InputTextWithHint(
            "Rule name", "named by its condition", nameBuffer.data(), nameBuffer.size());
        if (nameEdited)
        {
            if (nameBuffer[0] == '\0')
                EraseKey(layer, "name");
            else if (JsonValue* existing = layer.Find("name"))
                *existing = JsonValue(std::string(nameBuffer.data()));
            else
                layer.AsObject().emplace_back("name", JsonValue(std::string(nameBuffer.data())));
        }
        edit.Changed = edit.Changed || nameEdited;
        edit.Committed = edit.Committed || ImGui::IsItemDeactivatedAfterEdit();

        ImGui::Spacing();
        DimText("Applies when");
        edit |= DrawCondition(layer, layerSchema, preview);

        ImGui::Spacing();
        DimText("Changes");
        for (const PatchOperation& operation : kOperations)
        {
            JsonValue* patch = FindOrNull(layer, operation.Key);
            if (patch == nullptr || !patch->IsObject() || patch->AsObject().empty())
                continue;

            ImGui::PushID(operation.Key);
            ImGui::TextDisabled("%s", operation.Verb);
            const DataFieldSchema* patchSchema = FindChild(layerSchema, operation.Key);
            for (const MovementCoefficientLabel& label : labels)
            {
                bool removed = false;
                edit |= DrawCoefficientRow(
                    *patch, label,
                    patchSchema ? FindChild(*patchSchema, label.Key) : nullptr,
                    std::format("{}.{}.{}", layerPath, operation.Key, label.Key),
                    workspace, removed);
                if (removed)
                {
                    EraseKey(*patch, label.Key);
                    edit |= FieldEdit::Instant();
                }
            }
            if (patch->IsObject() && patch->AsObject().empty())
                EraseKey(layer, operation.Key);
            ImGui::PopID();
        }

        for (std::size_t index = 0; index < kOperations.size(); ++index)
        {
            if (index > 0)
                ImGui::SameLine();
            edit |= DrawAddCoefficient(layer, kOperations[index], labels);
        }
        return edit;
    }

    // One card per layer: name and condition on the header line, what it does
    // underneath, controls behind the expander.
    FieldEdit DrawLayerCards(JsonValue& layersValue,
                             const DataFieldSchema& layersSchema,
                             const std::vector<MovementProfileLayer>* compiled,
                             std::span<const MovementLayerTrace> trace,
                             const std::vector<MovementCoefficientLabel>& labels,
                             const MovementResolvePreview& preview,
                             const std::string& path,
                             DataEditorWorkspace& workspace)
    {
        FieldEdit edit;
        if (!layersValue.IsArray())
            layersValue = JsonValue(JsonValue::Array{});
        if (layersSchema.Children.empty())
            return edit;

        const DataFieldSchema& layerSchema = layersSchema.Children.front();
        JsonValue::Array& layers = layersValue.AsArray();
        std::optional<std::size_t> remove;

        for (std::size_t index = 0; index < layers.size(); ++index)
        {
            ImGui::PushID(static_cast<int>(index));

            const std::string elementPath = std::format("{}[{}]", path, index);
            const bool haveCompiled = compiled != nullptr && index < compiled->size();
            const std::string name = haveCompiled
                ? MovementLayerName((*compiled)[index])
                : std::format("Layer {}", index + 1);
            const std::string condition = haveCompiled
                ? DescribeMovementCondition((*compiled)[index].When)
                : std::string{};
            const std::string changes = haveCompiled
                ? DescribeMovementPatches((*compiled)[index], labels)
                : std::string{};

            const bool open = ImGui::TreeNodeEx("##layer", ImGuiTreeNodeFlags_AllowOverlap,
                                                "%s", name.c_str());

            // Whether this layer is live under the simulated context, so the
            // author can see the ruleset react rather than infer it.
            if (index < trace.size())
            {
                ImGui::SameLine();
                if (trace[index].Matched)
                {
                    ImGui::TextColored(EditorUi::Success, "active");
                }
                else
                {
                    ImGui::TextDisabled("inactive");
                    if (ImGui::IsItemHovered() && !trace[index].Failure.empty())
                        ImGui::SetTooltip("%s", trace[index].Failure.c_str());
                }
            }

            if (!condition.empty())
            {
                ImGui::SameLine();
                const float right = ImGui::GetContentRegionAvail().x
                    - ImGui::CalcTextSize(condition.c_str()).x;
                if (right > 0.0f)
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + right);
                ImGui::TextDisabled("%s", condition.c_str());
            }

            if (!changes.empty())
            {
                ImGui::Indent();
                DimText(changes);
                ImGui::Unindent();
            }

            if (open)
            {
                ImGui::Indent();
                edit |= DrawLayerBody(layers[index], layerSchema, labels, preview,
                                      elementPath, workspace);

                ButtonFlow verbs;
                if (verbs.Button("Duplicate"))
                {
                    layers.insert(layers.begin() + static_cast<std::ptrdiff_t>(index + 1),
                                  layers[index]);
                    edit |= FieldEdit::Instant();
                }
                if (verbs.Button("Delete"))
                    remove = index;
                if (index > 0 && verbs.Button("Move up"))
                {
                    std::swap(layers[index], layers[index - 1]);
                    edit |= FieldEdit::Instant();
                }
                if (index + 1 < layers.size() && verbs.Button("Move down"))
                {
                    std::swap(layers[index], layers[index + 1]);
                    edit |= FieldEdit::Instant();
                }
                ImGui::Unindent();
                ImGui::TreePop();
            }

            ImGui::Separator();
            ImGui::PopID();
            if (remove)
                break;
        }

        if (remove)
        {
            layers.erase(layers.begin() + static_cast<std::ptrdiff_t>(*remove));
            edit |= FieldEdit::Instant();
        }
        if (ImGui::Button("Add rule"))
        {
            layers.push_back(JsonValue(JsonValue::Object{}));
            edit |= FieldEdit::Instant();
        }
        ImGui::SetItemTooltip("Later rules win where they overlap earlier ones.");
        return edit;
    }
}

std::string_view MovementProfileSubtype()
{
    return kSubtype;
}

FieldEdit DrawMovementProfileForm(JsonValue& data,
                                  const DataSchema& schema,
                                  DataEditorWorkspace& workspace,
                                  MovementResolvePreview& preview)
{
    FieldEdit edit;
    if (!data.IsObject())
        return edit;

    const std::vector<MovementCoefficientLabel> labels = MovementCoefficientLabels(schema);

    if (JsonValue* name = data.Find("name"))
    {
        std::array<char, 256> buffer{};
        if (name->IsString())
        {
            const std::string& text = name->AsString();
            std::memcpy(buffer.data(), text.c_str(), std::min(text.size(), buffer.size() - 1));
        }
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
        const bool edited = ImGui::InputText("Profile name", buffer.data(), buffer.size());
        if (edited)
            *name = JsonValue(std::string(buffer.data()));
        edit.Changed = edit.Changed || edited;
        edit.Committed = edit.Committed || ImGui::IsItemDeactivatedAfterEdit();
    }

    if (!preview.IsBound() && !preview.Error().empty())
        ImGui::TextColored(EditorUi::Warning, "%s", preview.Error().c_str());

    const MovementResolveResult resolved = preview.Resolve();
    const CompiledMovementProfile* compiled = preview.Compiled();

    ImGui::Spacing();
    ImGui::SeparatorText("Rules, in order");
    const DataFieldSchema* layersSchema = FindChild(schema.Root, "layers");

    if (layersSchema != nullptr)
    {
        JsonValue& layers = EnsureMember(data, "layers", JsonValue(JsonValue::Array{}));
        const std::size_t shared = compiled ? compiled->Layers.size() : 0;
        const std::span<const MovementLayerTrace> trace =
            resolved.Trace.size() >= shared
                ? std::span<const MovementLayerTrace>(resolved.Trace.data(), shared)
                : std::span<const MovementLayerTrace>{};
        edit |= DrawLayerCards(layers, *layersSchema,
                               compiled ? &compiled->Layers : nullptr,
                               trace, labels, preview, "$.data.layers", workspace);
    }

    // Modes have no purpose-built surface, so they get the schema-generated one
    // rather than being editable only through raw JSON. Their layers still read
    // as titled cards, because that presentation is authored in the schema.
    if (const DataFieldSchema* modesSchema = FindChild(schema.Root, "modes"))
    {
        ImGui::Spacing();
        ImGui::SeparatorText("Modes");
        ImGui::TextDisabled("Tuning that applies only while a locomotion mode is active.");

        JsonValue& modes = EnsureMember(data, "modes", JsonValue(JsonValue::Array{}));
        edit |= DrawDataField(modes, *modesSchema, "$.data.modes", workspace);

        // The whole document is committed on any change, so an untouched empty
        // array should not start appearing in the file.
        if (modes.IsArray() && modes.AsArray().empty())
            EraseKey(data, "modes");
    }

    return edit;
}
