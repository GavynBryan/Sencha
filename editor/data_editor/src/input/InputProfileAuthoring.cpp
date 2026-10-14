#include "input/InputProfileAuthoring.h"

#include "data/JsonObjectEdit.h"
#include "input/InputBindingSummary.h"
#include "input/InputControlCapture.h"
#include "input/InputProfilePreview.h"
#include "input/InputSlotAcceptance.h"

#include "data/DataDocument.h"
#include "data/DataPath.h"
#include "properties/PropertyTree.h"
#include "properties/SettingProperties.h"

#include <input/InputBindings.h>
#include <input/InputControl.h>

#include <algorithm>
#include <array>
#include <format>

namespace
{
constexpr std::string_view kSubtype = "input.profile";
// The key of a binding's shape, which no member of the file holds.
constexpr std::string_view kShapeSuffix = "~shape";

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
    std::string_view Label;
    std::string_view Summary;
};

constexpr std::array<ShapeChoice, 3> kShapes{ {
    { BindingShape::SingleControl, "Single control", "One key, button, stick, or the mouse drives the action." },
    { BindingShape::AxisPair, "Two buttons (axis)", "One button drives the value negative, the other positive." },
    { BindingShape::Cardinal, "Four directions", "Four buttons drive a plane, the way WASD does." },
} };

struct Slot
{
    std::string_view Key;
    std::string_view Label;
};
constexpr std::array<Slot, 1> kControlSlot{ { { "control", "Control" } } };
constexpr std::array<Slot, 2> kAxisSlots{ { { "negative", "Negative" }, { "positive", "Positive" } } };
constexpr std::array<Slot, 4> kCardinalSlots{ { { "left", "Left" }, { "right", "Right" }, { "down", "Down" }, { "up", "Up" } } };

BindingShape ReadShape(const JsonValue& binding)
{
    const std::string composite = ReadMemberString(binding, "composite");
    if (composite == "cardinal")
        return BindingShape::Cardinal;
    if (composite == "axis")
        return BindingShape::AxisPair;
    return BindingShape::SingleControl;
}

std::span<const Slot> SlotsOf(BindingShape shape)
{
    switch (shape)
    {
    case BindingShape::Cardinal: return kCardinalSlots;
    case BindingShape::AxisPair: return kAxisSlots;
    case BindingShape::SingleControl: break;
    }
    return kControlSlot;
}

// Writing the shape erases the slots the other shapes use. A binding that kept
// them would carry values the author cannot see and the compiler rejects.
void ApplyShape(JsonValue& binding, BindingShape shape)
{
    EraseMember(binding, "control");
    EraseMember(binding, "composite");
    for (const std::span<const Slot> slots : { std::span<const Slot>(kAxisSlots), std::span<const Slot>(kCardinalSlots) })
        for (const Slot& slot : slots)
            EraseMember(binding, slot.Key);
    if (shape == BindingShape::AxisPair)
        SetMember(binding, "composite", JsonValue(std::string("axis")));
    else if (shape == BindingShape::Cardinal)
        SetMember(binding, "composite", JsonValue(std::string("cardinal")));
    for (const Slot& slot : SlotsOf(shape))
        SetMember(binding, slot.Key, JsonValue(std::string{}));
}

InputBindingKind ShapeKind(BindingShape shape)
{
    switch (shape)
    {
    case BindingShape::Cardinal: return InputBindingKind::Cardinal;
    case BindingShape::AxisPair: return InputBindingKind::AxisPair;
    case BindingShape::SingleControl: break;
    }
    return InputBindingKind::Direct;
}

// Whether a binding of this shape can drive an action of that type, by the same
// rule the runtime binder applies.
bool ShapeCanDrive(BindingShape shape, const JsonValue& binding, InputActionType actionType)
{
    InputBinding probe;
    probe.Kind = ShapeKind(shape);
    if (shape == BindingShape::SingleControl)
    {
        const std::optional<InputControl> parsed = ParseInputControl(ReadMemberString(binding, "control"));
        // An empty slot cannot be judged yet; every action is offered until the
        // author has picked a control.
        if (!parsed.has_value())
            return true;
        probe.Controls[kBindingNegativeX] = *parsed;
    }
    return BindingProducesType(probe, actionType);
}

// "$.data.contexts[i].bindings[j]" and the member after it, for a key under a binding.
struct BindingKey
{
    std::string Binding;
    std::string Member;
};

std::optional<BindingKey> BindingOf(std::string_view key)
{
    const bool shape = key.ends_with(kShapeSuffix);
    const std::string_view path = shape ? key.substr(0, key.size() - kShapeSuffix.size()) : key;
    const std::optional<std::vector<DataPathStep>> steps = ParseDataPath(path);
    if (!steps.has_value() || steps->size() < 4 || (*steps)[0].Key != "contexts" || !(*steps)[1].Index.has_value()
        || (*steps)[2].Key != "bindings" || !(*steps)[3].Index.has_value())
        return std::nullopt;
    const std::string binding = std::format("$.data.contexts[{}].bindings[{}]", *(*steps)[1].Index, *(*steps)[3].Index);
    if (shape)
        return steps->size() == 4 ? std::optional<BindingKey>({ binding, std::string(kShapeSuffix) }) : std::nullopt;
    return steps->size() == 5 && !(*steps)[4].Index.has_value() ? std::optional<BindingKey>({ binding, (*steps)[4].Key })
                                                                : std::nullopt;
}

bool IsSlot(const JsonValue& binding, std::string_view member)
{
    const std::span<const Slot> slots = SlotsOf(ReadShape(binding));
    return std::ranges::any_of(slots, [member](const Slot& slot) { return slot.Key == member; });
}

class InputProfileAuthoring final : public IDataSubtypeAuthoring
{
public:
    [[nodiscard]] std::string_view Subtype() const override { return kSubtype; }

    void Update(const DataDocument& document, DataDocumentSet& documents) override
    {
        Preview.Update(document, documents);
    }

    void Refine(std::vector<PropertyNode>& nodes, const DataDocument& document, const DataSchema&) const override
    {
        const JsonValue* data = document.Data();
        if (data == nullptr)
            return;
        if (std::optional<PropertyPlace> actions = FindPropertyNode(nodes, "$.data.actions");
            actions.has_value() && !Preview.ActionSetError().empty())
            actions->Siblings->insert(actions->Siblings->begin() + static_cast<std::ptrdiff_t>(actions->Index + 1),
                                      NoteProperty("$.data.actions!set", "Action set", Preview.ActionSetError()));
        const JsonValue* contexts = FindMember(*data, "contexts");
        const std::size_t contextCount = contexts != nullptr && contexts->IsArray() ? contexts->AsArray().size() : 0;
        for (std::size_t c = 0; c < contextCount; ++c)
        {
            const JsonValue& context = contexts->AsArray()[c];
            const std::string contextPath = std::format("$.data.contexts[{}]", c);
            if (std::optional<PropertyPlace> card = FindPropertyNode(nodes, contextPath))
            {
                const std::string name = ReadMemberString(context, "name");
                card->Node().Descriptor.Label = name.empty() ? std::format("Context {}", c + 1) : name;
                card->Node().Descriptor.Tooltip = InputContextCardSubtitle(context);
            }
            const JsonValue* bindings = FindMember(context, "bindings");
            const std::size_t bindingCount = bindings != nullptr && bindings->IsArray() ? bindings->AsArray().size() : 0;
            for (std::size_t b = 0; b < bindingCount; ++b)
                RefineBinding(nodes, bindings->AsArray()[b], std::format("{}.bindings[{}]", contextPath, b), document);
        }
        if (const std::span<const std::string> unbound = Preview.UnboundActions(); !unbound.empty())
            if (std::optional<PropertyPlace> root = FindPropertyNode(nodes, "$.data"))
            {
                std::string text;
                for (const std::string& action : unbound)
                    text += (text.empty() ? "" : ", ") + action;
                root->Node().Children.push_back(NoteProperty("$.data!unbound", "Not bound", text));
            }
    }

    [[nodiscard]] DataSubtypeEdit Write(std::string_view key, std::string_view value, JsonValue& data,
                                        const DataSchema&) const override
    {
        const std::optional<BindingKey> at = BindingOf(key);
        if (!at.has_value() || at->Member != kShapeSuffix)
            return DataSubtypeEdit::NotMine;
        JsonValue* binding = FindDataValue(data, at->Binding);
        const auto shape = std::ranges::find(kShapes, value, &ShapeChoice::Label);
        if (binding == nullptr || shape == kShapes.end())
            return DataSubtypeEdit::Refused;
        if (shape->Shape != ReadShape(*binding))
            ApplyShape(*binding, shape->Shape);
        return DataSubtypeEdit::Applied;
    }

    [[nodiscard]] std::optional<std::vector<std::string>> Candidates(std::string_view key,
                                                                     const JsonValue& data) const override
    {
        const std::optional<BindingKey> at = BindingOf(key);
        const JsonValue* binding = at.has_value() ? FindDataValue(data, at->Binding) : nullptr;
        if (binding == nullptr || !IsSlot(*binding, at->Member))
            return std::nullopt;
        // The engine's rule, not a copy of it: a control the binder would reject
        // is never offered.
        const InputSlotAcceptance acceptance = AcceptanceOf(*binding);
        std::vector<std::string> controls;
        for (const NamedInputControl& entry : EnumerateInputControls())
            if (acceptance.Accepts(entry.Control.Source))
                controls.push_back(entry.Name);
        return controls;
    }

    [[nodiscard]] DataSubtypeEdit Act(std::string_view key, std::string_view verb,
                                      const DataDocument& document) override
    {
        if (verb != "listen")
            return DataSubtypeEdit::NotMine;
        const std::optional<BindingKey> at = BindingOf(key);
        const JsonValue* binding = at.has_value() && document.Data() != nullptr ? FindDataValue(*document.Data(), at->Binding)
                                                                                : nullptr;
        if (binding == nullptr || !IsSlot(*binding, at->Member))
            return DataSubtypeEdit::Gone;
        if (Capture.IsArmedAt(key))
            Capture.Disarm();
        else
            Capture.Arm(std::string(key), AcceptanceOf(*binding), document.VirtualPath(), document.Revision());
        return DataSubtypeEdit::Applied;
    }

    bool HandlePlatformEvent(const SDL_Event& event) override { return Capture.HandleSdlEvent(event); }

    [[nodiscard]] std::optional<std::pair<std::string, std::string>> TakeWrite(const DataDocument& document) override
    {
        const std::string field = Capture.PendingField();
        if (field.empty())
            return std::nullopt;
        std::optional<std::string> control = Capture.TakeCapture(field, document.VirtualPath(), document.Revision());
        if (!control.has_value())
            return std::nullopt;
        return std::pair{ field, std::move(*control) };
    }

private:
    // What this binding's slots may hold: its shape, and the action it drives
    // when the set declares one.
    [[nodiscard]] InputSlotAcceptance AcceptanceOf(const JsonValue& binding) const
    {
        InputSlotAcceptance acceptance;
        acceptance.Kind = ShapeKind(ReadShape(binding));
        const std::string action = ReadMemberString(binding, "action");
        for (const KnownInputAction& known : Preview.KnownActions())
            if (known.Name == action)
            {
                acceptance.ActionType = known.Type;
                acceptance.ActionKnown = true;
            }
        return acceptance;
    }

    void RefineBinding(std::vector<PropertyNode>& nodes, const JsonValue& binding, const std::string& path,
                       const DataDocument&) const
    {
        std::optional<PropertyPlace> card = FindPropertyNode(nodes, path);
        if (!card.has_value())
            return;
        PropertyNode& node = card->Node();
        node.Descriptor.Label = InputBindingCardTitle(binding);
        std::vector<PropertyNode>& members = node.Children;
        std::erase_if(members, [&](const PropertyNode& member) { return member.Key == path + ".composite"; });

        const BindingShape shape = ReadShape(binding);
        const std::string action = ReadMemberString(binding, "action");
        const auto actionAt = std::ranges::find(members, path + ".action", &PropertyNode::Key);
        if (actionAt != members.end())
        {
            PropertyNode& chosen = *actionAt;
            chosen.Descriptor.Kind = PropertyKind::Choice;
            chosen.Descriptor.Choices.clear();
            bool known = action.empty();
            for (const KnownInputAction& candidate : Preview.KnownActions())
            {
                known = known || candidate.Name == action;
                if (candidate.Name == action || ShapeCanDrive(shape, binding, candidate.Type))
                    chosen.Descriptor.Choices.push_back(candidate.Name);
            }
            // An action the set does not declare is kept, not cleared: it may be
            // one the author is about to add. None chosen yet is offered as it is.
            if (std::ranges::find(chosen.Descriptor.Choices, action) == chosen.Descriptor.Choices.end())
                chosen.Descriptor.Choices.insert(chosen.Descriptor.Choices.begin(), action);
            const std::size_t after = static_cast<std::size_t>(actionAt - members.begin()) + 1;
            std::array<std::string_view, kShapes.size()> labels{};
            for (std::size_t i = 0; i < kShapes.size(); ++i)
                labels[i] = kShapes[i].Label;
            const auto current = std::ranges::find(kShapes, shape, &ShapeChoice::Shape);
            PropertyNode shapeNode = ChoiceProperty(path + std::string(kShapeSuffix), "Bound to", labels,
                                                    static_cast<std::size_t>(current - kShapes.begin()),
                                                    std::string(current->Summary));
            members.insert(members.begin() + static_cast<std::ptrdiff_t>(after), std::move(shapeNode));
            if (!known)
                members.insert(members.begin() + static_cast<std::ptrdiff_t>(after),
                               NoteProperty(path + ".action!declared", "Action",
                                            std::format("'{}' is not declared by the action set", action)));
        }

        const InputSlotAcceptance acceptance = AcceptanceOf(binding);
        for (const Slot& slot : SlotsOf(shape))
        {
            const std::string key = path + "." + std::string(slot.Key);
            const auto at = std::ranges::find(members, key, &PropertyNode::Key);
            if (at == members.end())
                continue;
            at->Descriptor.Kind = PropertyKind::Asset;
            at->Descriptor.Label = std::string(slot.Label);
            at->Descriptor.Tooltip = "Pick a control, or listen and press it; Escape stops listening";
            // The shape says which slots there are, so none is removed on its own.
            std::erase_if(at->Descriptor.Verbs, [](const PropertyVerb& verb) { return verb.Id == "remove"; });
            at->Descriptor.Verbs.push_back(
                { .Id = "listen", .Label = Capture.IsArmedAt(key) ? "Press a control..." : "Listen" });
            // The two mistakes this field invites, said at the field.
            const std::string current = ReadMemberString(binding, slot.Key);
            const std::optional<InputControl> parsed = current.empty() ? std::nullopt : ParseInputControl(current);
            std::string problem;
            if (!current.empty() && !parsed.has_value())
                problem = std::format("'{}' is not a control on this platform", current);
            else if (parsed.has_value() && !acceptance.Accepts(parsed->Source))
                problem = std::format("'{}' cannot drive this action", current);
            if (!problem.empty())
                members.insert(at + 1, NoteProperty(key + "!control", std::string(slot.Label), problem));
        }
    }

    InputProfilePreview Preview;
    InputControlCapture Capture;
};
} // namespace

std::unique_ptr<IDataSubtypeAuthoring> CreateInputProfileAuthoring()
{
    return std::make_unique<InputProfileAuthoring>();
}
