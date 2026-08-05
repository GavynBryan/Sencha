#include "input/InputProfileForm.h"

#include "DataDocument.h"
#include "DataEditorWorkspace.h"
#include "JsonObjectEdit.h"
#include "input/InputBindingSummary.h"
#include "input/InputControlCapture.h"
#include "input/InputControlPicker.h"
#include "input/InputProfilePreview.h"

#include "ui/ButtonFlow.h"
#include "ui/EditorUiStyle.h"

#include <core/metadata/DataSchema.h>
#include <input/InputBindings.h>
#include <input/InputControl.h>

#include <imgui.h>

#include <array>
#include <format>
#include <optional>
#include <string>

namespace
{
constexpr std::string_view kSubtype = "input.profile";

// How a binding produces its value, in the author's terms. The mechanical
// fields -- control, composite, and which slots exist -- follow from the
// choice, so the shape is picked once instead of assembled by hand.
enum class BindingShape : std::uint8_t
{
    SingleControl,
    AxisPair,
    Cardinal,
};

struct ShapeChoice
{
    BindingShape Shape;
    const char* Label;
    const char* Summary;
};

constexpr std::array<ShapeChoice, 3> kShapes{ {
    { BindingShape::SingleControl, "Single control",
      "One key, button, stick, or the mouse drives the action." },
    { BindingShape::AxisPair, "Two buttons (axis)",
      "One button drives the value negative, the other positive." },
    { BindingShape::Cardinal, "Four directions",
      "Four buttons drive a plane, the way WASD does." },
} };

// The four cardinal slots in schema order, and the two axis slots.
constexpr std::array<std::pair<const char*, const char*>, 4> kCardinalSlots{ {
    { "left", "Left" }, { "right", "Right" }, { "down", "Down" }, { "up", "Up" },
} };
constexpr std::array<std::pair<const char*, const char*>, 2> kAxisSlots{ {
    { "negative", "Negative" }, { "positive", "Positive" },
} };

BindingShape ReadShape(const JsonValue& binding)
{
    const std::string composite = ReadMemberString(binding, "composite");
    if (composite == "cardinal")
        return BindingShape::Cardinal;
    if (composite == "axis")
        return BindingShape::AxisPair;
    return BindingShape::SingleControl;
}

const char* ShapeLabel(BindingShape shape)
{
    for (const ShapeChoice& choice : kShapes)
    {
        if (choice.Shape == shape)
            return choice.Label;
    }
    return kShapes.front().Label;
}

// Writing the shape erases the slots the other shapes use. A binding that kept
// them would carry values the author cannot see and the compiler rejects --
// "exactly one of control or composite" is unviolatable from this form.
void ApplyShape(JsonValue& binding, BindingShape shape)
{
    EraseMember(binding, "control");
    EraseMember(binding, "composite");
    for (const auto& [key, label] : kCardinalSlots)
    {
        (void)label;
        EraseMember(binding, key);
    }
    for (const auto& [key, label] : kAxisSlots)
    {
        (void)label;
        EraseMember(binding, key);
    }

    switch (shape)
    {
    case BindingShape::SingleControl:
        SetMember(binding, "control", JsonValue(std::string{}));
        break;
    case BindingShape::AxisPair:
        SetMember(binding, "composite", JsonValue(std::string("axis")));
        for (const auto& [key, label] : kAxisSlots)
        {
            (void)label;
            SetMember(binding, key, JsonValue(std::string{}));
        }
        break;
    case BindingShape::Cardinal:
        SetMember(binding, "composite", JsonValue(std::string("cardinal")));
        for (const auto& [key, label] : kCardinalSlots)
        {
            (void)label;
            SetMember(binding, key, JsonValue(std::string{}));
        }
        break;
    }
}

// What a binding of this shape produces, so the action list can say which
// actions it could drive.
InputActionType ShapeProduces(BindingShape shape, const JsonValue& binding)
{
    switch (shape)
    {
    case BindingShape::Cardinal:
        return InputActionType::Axis2D;
    case BindingShape::AxisPair:
        return InputActionType::Axis1D;
    case BindingShape::SingleControl:
        break;
    }

    const std::string control = ReadMemberString(binding, "control");
    const std::optional<InputControl> parsed = ParseInputControl(control);
    if (!parsed.has_value())
        return InputActionType::Digital;
    switch (ValueKindOf(parsed->Source))
    {
    case InputControlValueKind::Axis2D:
        return InputActionType::Axis2D;
    case InputControlValueKind::Axis1D:
        return InputActionType::Axis1D;
    case InputControlValueKind::Button:
        break;
    }
    return InputActionType::Digital;
}

const char* ActionTypeLabel(InputActionType type)
{
    switch (type)
    {
    case InputActionType::Axis1D: return "Axis";
    case InputActionType::Axis2D: return "Plane";
    case InputActionType::Digital: break;
    }
    return "Button";
}

// Whether a binding of this shape can drive an action of that type, by the same
// rule the runtime binder applies.
bool ShapeCanDrive(BindingShape shape, const JsonValue& binding, InputActionType actionType)
{
    InputBinding probe;
    switch (shape)
    {
    case BindingShape::Cardinal:
        probe.Kind = InputBindingKind::Cardinal;
        break;
    case BindingShape::AxisPair:
        probe.Kind = InputBindingKind::AxisPair;
        break;
    case BindingShape::SingleControl:
    {
        probe.Kind = InputBindingKind::Direct;
        const std::optional<InputControl> parsed =
            ParseInputControl(ReadMemberString(binding, "control"));
        // An empty slot cannot be judged yet; let every action through rather
        // than disabling the list before the author has picked a control.
        if (!parsed.has_value())
            return true;
        probe.Controls[kBindingNegativeX] = *parsed;
        break;
    }
    }
    return BindingProducesType(probe, actionType);
}

FieldEdit DrawActionRow(JsonValue& binding,
                        const InputProfilePreview& preview,
                        BindingShape shape)
{
    FieldEdit edit;
    const std::string current = ReadMemberString(binding, "action");
    const std::span<const KnownInputAction> actions = preview.KnownActions();

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.4f);
    if (ImGui::BeginCombo("Action", current.empty() ? "(none)" : current.c_str()))
    {
        if (actions.empty())
            ImGui::TextDisabled("The action set declares nothing yet.");

        for (const KnownInputAction& action : actions)
        {
            const bool compatible = ShapeCanDrive(shape, binding, action.Type);
            const std::string label =
                std::format("{} ({})", action.Name, ActionTypeLabel(action.Type));

            ImGui::BeginDisabled(!compatible);
            if (ImGui::Selectable(label.c_str(), action.Name == current))
            {
                SetMember(binding, "action", JsonValue(action.Name));
                edit |= FieldEdit::Instant();
            }
            ImGui::EndDisabled();

            if (!compatible && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::SetTooltip("%s is a %s action; this binding produces a %s.",
                                  action.Name.c_str(), ActionTypeLabel(action.Type),
                                  ActionTypeLabel(ShapeProduces(shape, binding)));
            }
        }
        ImGui::EndCombo();
    }

    // An action the set does not declare is kept, not cleared: it may be one the
    // author is about to add, and silently dropping authored data is worse than
    // showing it as wrong.
    const bool known = current.empty()
        || std::any_of(actions.begin(), actions.end(),
                       [&current](const KnownInputAction& a) { return a.Name == current; });
    if (!known)
    {
        ImGui::Indent();
        ImGui::TextColored(EditorUi::Warning, "'%s' is not declared by the action set.",
                           current.c_str());
        ImGui::Unindent();
    }
    return edit;
}

FieldEdit DrawShapeRow(JsonValue& binding, BindingShape shape)
{
    FieldEdit edit;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.4f);
    if (ImGui::BeginCombo("Bound to", ShapeLabel(shape)))
    {
        for (const ShapeChoice& choice : kShapes)
        {
            if (ImGui::Selectable(choice.Label, choice.Shape == shape)
                && choice.Shape != shape)
            {
                ApplyShape(binding, choice.Shape);
                edit |= FieldEdit::Instant();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", choice.Summary);
        }
        ImGui::EndCombo();
    }
    return edit;
}

// Conditioning is optional and mostly absent; a row per setting on every
// binding would bury the two that are usually set.
FieldEdit DrawConditioning(JsonValue& binding, const std::string& path)
{
    FieldEdit edit;
    const auto drawFloat = [&](const char* key, const char* label, float min, float max)
    {
        JsonValue* value = FindMember(binding, key);
        if (value == nullptr || !value->IsNumber())
            return;
        auto current = static_cast<float>(value->AsNumber());
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.3f);
        const bool dragged = ImGui::DragFloat(label, &current, 0.005f, min, max, "%.4f");
        if (dragged)
            *value = JsonValue(static_cast<double>(current));
        edit |= FieldEdit{ dragged, ImGui::IsItemDeactivatedAfterEdit() };
        ImGui::SameLine();
        if (ImGui::SmallButton((std::string("Remove##") + key).c_str()))
        {
            EraseMember(binding, key);
            edit |= FieldEdit::Instant();
        }
    };

    const auto drawBool = [&](const char* key, const char* label)
    {
        JsonValue* value = FindMember(binding, key);
        if (value == nullptr || !value->IsBool())
            return;
        bool current = value->AsBool();
        if (ImGui::Checkbox(label, &current))
        {
            *value = JsonValue(current);
            edit |= FieldEdit::Instant();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton((std::string("Remove##") + key).c_str()))
        {
            EraseMember(binding, key);
            edit |= FieldEdit::Instant();
        }
    };

    drawFloat("scale", "Scale", -1000.0f, 1000.0f);
    drawFloat("dead_zone", "Dead zone", 0.0f, 0.99f);
    drawBool("invert_x", "Invert X");
    drawBool("invert_y", "Invert Y");
    drawBool("normalize", "Clamp to unit");

    const std::string popup = "settings##" + path;
    if (ImGui::SmallButton("Add setting"))
        ImGui::OpenPopup(popup.c_str());
    if (ImGui::BeginPopup(popup.c_str()))
    {
        struct Setting
        {
            const char* Key;
            const char* Label;
            const char* Summary;
            JsonValue Default;
        };
        const std::array<Setting, 5> settings{ {
            { "scale", "Scale", "Multiply the resolved value.", JsonValue(1.0) },
            { "dead_zone", "Dead zone", "Ignore movement smaller than this.", JsonValue(0.2) },
            { "invert_x", "Invert X", "Flip the sign of the X result.", JsonValue(true) },
            { "invert_y", "Invert Y", "Flip the sign of the Y result.", JsonValue(true) },
            { "normalize", "Clamp to unit", "Keep diagonals from outrunning cardinals.",
              JsonValue(true) },
        } };

        bool anyOffered = false;
        for (const Setting& setting : settings)
        {
            if (FindMember(binding, setting.Key) != nullptr)
                continue;
            anyOffered = true;
            if (ImGui::Selectable(setting.Label))
            {
                SetMember(binding, setting.Key, setting.Default);
                edit |= FieldEdit::Instant();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", setting.Summary);
        }
        if (!anyOffered)
            ImGui::TextDisabled("Everything here is already set.");
        ImGui::EndPopup();
    }
    return edit;
}

FieldEdit DrawBindingBody(JsonValue& binding,
                          const InputProfilePreview& preview,
                          const std::string& path,
                          InputControlCapture& capture,
                          const DataDocument& document)
{
    FieldEdit edit;
    const BindingShape shape = ReadShape(binding);

    edit |= DrawActionRow(binding, preview, shape);
    edit |= DrawShapeRow(binding, shape);

    switch (shape)
    {
    case BindingShape::SingleControl:
        edit |= DrawInputControlSlot(binding, "control", "Control", path + ".control",
                                     InputControlSlotFilter::AnyControl, capture,
                                     document.VirtualPath(), document.Revision());
        break;
    case BindingShape::AxisPair:
        for (const auto& [key, label] : kAxisSlots)
        {
            edit |= DrawInputControlSlot(binding, key, label, path + "." + key,
                                         InputControlSlotFilter::ButtonsOnly, capture,
                                         document.VirtualPath(), document.Revision());
        }
        break;
    case BindingShape::Cardinal:
        for (const auto& [key, label] : kCardinalSlots)
        {
            edit |= DrawInputControlSlot(binding, key, label, path + "." + key,
                                         InputControlSlotFilter::ButtonsOnly, capture,
                                         document.VirtualPath(), document.Revision());
        }
        break;
    }

    edit |= DrawConditioning(binding, path);
    return edit;
}

// A marker on a collapsed card, so a mistake inside one is visible without
// opening every card to hunt for it.
void DrawErrorMarker(const DataDocument& document, const std::string& path)
{
    const DataValidationError* error =
        FindValidationErrorAt(document.ValidationErrors(), path);
    if (error == nullptr)
        return;
    ImGui::SameLine();
    ImGui::TextColored(EditorUi::Warning, "(!)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", error->Message.c_str());
}

FieldEdit DrawBindings(JsonValue& context,
                       const InputProfilePreview& preview,
                       const DataDocument& document,
                       const std::string& contextPath,
                       InputControlCapture& capture)
{
    FieldEdit edit;
    JsonValue* bindingsValue = FindMember(context, "bindings");
    if (bindingsValue == nullptr || !bindingsValue->IsArray())
    {
        SetMember(context, "bindings", JsonValue(JsonValue::Array{}));
        bindingsValue = FindMember(context, "bindings");
        edit |= FieldEdit::Instant();
    }

    JsonValue::Array& bindings = bindingsValue->AsArray();
    std::optional<std::size_t> remove;
    for (std::size_t index = 0; index < bindings.size(); ++index)
    {
        ImGui::PushID(static_cast<int>(index));
        const std::string path = std::format("{}.bindings[{}]", contextPath, index);
        const std::string title = InputBindingCardTitle(bindings[index]);

        const bool open = ImGui::TreeNodeEx("##binding", ImGuiTreeNodeFlags_AllowOverlap,
                                            "%s", title.c_str());
        DrawErrorMarker(document, path);

        if (open)
        {
            ImGui::Indent();
            edit |= DrawBindingBody(bindings[index], preview, path, capture, document);

            ButtonFlow verbs;
            if (verbs.Button("Duplicate"))
            {
                bindings.insert(bindings.begin() + static_cast<std::ptrdiff_t>(index + 1),
                                bindings[index]);
                edit |= FieldEdit::Instant();
            }
            if (verbs.Button("Delete"))
                remove = index;
            if (index > 0 && verbs.Button("Move up"))
            {
                std::swap(bindings[index], bindings[index - 1]);
                edit |= FieldEdit::Instant();
            }
            if (index + 1 < bindings.size() && verbs.Button("Move down"))
            {
                std::swap(bindings[index], bindings[index + 1]);
                edit |= FieldEdit::Instant();
            }
            ImGui::Unindent();
            ImGui::TreePop();
        }
        ImGui::PopID();
        if (remove)
            break;
    }

    if (remove)
    {
        bindings.erase(bindings.begin() + static_cast<std::ptrdiff_t>(*remove));
        edit |= FieldEdit::Instant();
    }

    if (ImGui::Button("Add binding"))
    {
        JsonValue::Object binding;
        binding.emplace_back("action", JsonValue(std::string{}));
        binding.emplace_back("control", JsonValue(std::string{}));
        bindings.push_back(JsonValue(std::move(binding)));
        edit |= FieldEdit::Instant();
    }
    return edit;
}

FieldEdit DrawContextHeader(JsonValue& context)
{
    FieldEdit edit;

    std::array<char, 256> buffer{};
    const std::string name = ReadMemberString(context, "name");
    const std::size_t count = std::min(name.size(), buffer.size() - 1);
    std::memcpy(buffer.data(), name.c_str(), count);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.3f);
    const bool edited = ImGui::InputText("Name", buffer.data(), buffer.size());
    if (edited)
        SetMember(context, "name", JsonValue(std::string(buffer.data())));
    ImGui::SetItemTooltip("Gameplay switches this group on and off by this name.");
    edit |= FieldEdit{ edited, ImGui::IsItemDeactivatedAfterEdit() };

    JsonValue* priority = FindMember(context, "priority");
    if (priority == nullptr || !priority->IsNumber())
    {
        SetMember(context, "priority", JsonValue(0.0));
        priority = FindMember(context, "priority");
        edit |= FieldEdit::Instant();
    }
    auto value = static_cast<int>(priority->AsNumber());
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.3f);
    const bool dragged = ImGui::DragInt("Priority", &value, 1.0f);
    if (dragged)
        *priority = JsonValue(static_cast<double>(value));
    ImGui::SetItemTooltip("Where two active groups bind the same control, the higher "
                          "priority takes it and the lower one stops seeing it.");
    edit |= FieldEdit{ dragged, ImGui::IsItemDeactivatedAfterEdit() };
    return edit;
}
}

std::string_view InputProfileSubtype()
{
    return kSubtype;
}

FieldEdit DrawInputProfileForm(JsonValue& data,
                               const DataSchema& schema,
                               DataEditorWorkspace& workspace,
                               InputProfilePreview& preview,
                               InputControlCapture& capture)
{
    FieldEdit edit;
    const DataDocument* document = workspace.Active();
    if (document == nullptr)
        return edit;

    // The action-set reference keeps the schema-generated widget: the generic
    // renderer already offers a subtype-filtered picker and an Open button,
    // which is exactly what this field wants.
    if (const DataFieldSchema* actionSet = FindChild(schema.Root, "actions"))
    {
        JsonValue* value = FindMember(data, "actions");
        if (value == nullptr)
        {
            SetMember(data, "actions", JsonValue(std::string{}));
            value = FindMember(data, "actions");
            edit |= FieldEdit::Instant();
        }
        edit |= DrawDataField(*value, *actionSet, "$.data.actions", workspace);
    }

    if (!preview.ActionSetError().empty())
        ImGui::TextColored(EditorUi::Warning, "%s", preview.ActionSetError().c_str());

    ImGui::Separator();

    JsonValue* contextsValue = FindMember(data, "contexts");
    if (contextsValue == nullptr || !contextsValue->IsArray())
    {
        SetMember(data, "contexts", JsonValue(JsonValue::Array{}));
        contextsValue = FindMember(data, "contexts");
        edit |= FieldEdit::Instant();
    }

    JsonValue::Array& contexts = contextsValue->AsArray();
    if (contexts.empty())
        ImGui::TextDisabled("No contexts yet. A context is a group of bindings the game "
                            "switches on and off.");

    std::optional<std::size_t> remove;
    for (std::size_t index = 0; index < contexts.size(); ++index)
    {
        ImGui::PushID(static_cast<int>(index));
        const std::string path = std::format("$.data.contexts[{}]", index);
        const std::string name = ReadMemberString(contexts[index], "name");
        const std::string title = name.empty() ? std::format("Context {}", index + 1) : name;
        const std::string subtitle = InputContextCardSubtitle(contexts[index]);

        const bool open = ImGui::TreeNodeEx("##context", ImGuiTreeNodeFlags_AllowOverlap,
                                            "%s", title.c_str());
        DrawErrorMarker(*document, path);
        if (!subtitle.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", subtitle.c_str());
        }

        if (open)
        {
            ImGui::Indent();
            edit |= DrawContextHeader(contexts[index]);
            edit |= DrawBindings(contexts[index], preview, *document, path, capture);

            ButtonFlow verbs;
            if (verbs.Button("Duplicate"))
            {
                contexts.insert(contexts.begin() + static_cast<std::ptrdiff_t>(index + 1),
                                contexts[index]);
                edit |= FieldEdit::Instant();
            }
            if (verbs.Button("Delete"))
                remove = index;
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
        contexts.erase(contexts.begin() + static_cast<std::ptrdiff_t>(*remove));
        edit |= FieldEdit::Instant();
    }

    if (ImGui::Button("Add context"))
    {
        JsonValue::Object context;
        context.emplace_back("name", JsonValue(std::string{}));
        context.emplace_back("priority", JsonValue(0.0));
        context.emplace_back("bindings", JsonValue(JsonValue::Array{}));
        contexts.push_back(JsonValue(std::move(context)));
        edit |= FieldEdit::Instant();
    }

    // Declared but never bound: the author's cue that an action they added is
    // still waiting for a control.
    if (const std::span<const std::string> unbound = preview.UnboundActions(); !unbound.empty())
    {
        std::string text = "Declared but not bound in this profile: ";
        for (std::size_t i = 0; i < unbound.size(); ++i)
        {
            if (i > 0)
                text += ", ";
            text += unbound[i];
        }
        ImGui::Separator();
        ImGui::TextDisabled("%s", text.c_str());
    }
    return edit;
}
