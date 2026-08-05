#include "input/InputBindingSummary.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <unordered_set>

namespace
{
const JsonValue* FindString(const JsonValue& owner, std::string_view key)
{
    const JsonValue* value = owner.Find(key);
    return value != nullptr && value->IsString() ? value : nullptr;
}

std::string ReadString(const JsonValue& owner, std::string_view key)
{
    const JsonValue* value = FindString(owner, key);
    return value != nullptr ? value->AsString() : std::string{};
}

// "left_shift" -> "Left Shift". Authored names spell spaces as underscores so a
// control name stays one token; a label has no such constraint.
std::string TitleCaseWords(std::string_view text)
{
    std::string result;
    result.reserve(text.size());
    bool startOfWord = true;
    for (const char c : text)
    {
        if (c == '_')
        {
            result.push_back(' ');
            startOfWord = true;
            continue;
        }
        const auto lowered = static_cast<unsigned char>(c);
        result.push_back(startOfWord ? static_cast<char>(std::toupper(lowered))
                                     : static_cast<char>(std::tolower(lowered)));
        startOfWord = false;
    }
    return result;
}

struct FriendlyName
{
    std::string_view Authored;
    std::string_view Friendly;
};

// Only where the mechanical name reads badly. Everything else title-cases.
constexpr std::array<FriendlyName, 7> kMouseNames{ {
    { "left", "Left mouse button" },
    { "right", "Right mouse button" },
    { "middle", "Middle mouse button" },
    { "x1", "Mouse button 4" },
    { "x2", "Mouse button 5" },
    { "delta", "Mouse movement" },
    { "wheel", "Mouse wheel" },
} };

bool IsCardinal(const JsonValue& binding)
{
    return ReadString(binding, "composite") == "cardinal";
}

bool IsAxisPair(const JsonValue& binding)
{
    return ReadString(binding, "composite") == "axis";
}

// The four cardinal slots in the order the schema names them.
std::array<std::string, 4> CardinalControls(const JsonValue& binding)
{
    return { ReadString(binding, "left"), ReadString(binding, "right"),
             ReadString(binding, "down"), ReadString(binding, "up") };
}

std::string JoinDescribed(const std::vector<std::string>& controls)
{
    std::string joined;
    for (const std::string& control : controls)
    {
        if (control.empty())
            continue;
        if (!joined.empty())
            joined += " / ";
        joined += DescribeInputControlName(control);
    }
    return joined;
}

// Conditioning worth saying out loud on a collapsed card. Scale at one and a
// dead zone at zero change nothing, so naming them would be noise.
std::string DescribeConditioning(const JsonValue& binding)
{
    std::string text;
    if (const JsonValue* scale = binding.Find("scale");
        scale != nullptr && scale->IsNumber() && scale->AsNumber() != 1.0)
    {
        text += ", scale " + FormatInputScalar(scale->AsNumber());
    }
    if (const JsonValue* deadZone = binding.Find("dead_zone");
        deadZone != nullptr && deadZone->IsNumber() && deadZone->AsNumber() > 0.0)
    {
        text += ", dead zone " + FormatInputScalar(deadZone->AsNumber());
    }
    if (const JsonValue* threshold = binding.Find("threshold");
        threshold != nullptr && threshold->IsNumber())
    {
        text += ", past " + FormatInputScalar(threshold->AsNumber());
    }
    if (const JsonValue* invertY = binding.Find("invert_y");
        invertY != nullptr && invertY->IsBool() && invertY->AsBool())
    {
        text += ", inverted";
    }
    return text;
}
}

std::vector<KnownInputAction> CollectInputActions(const JsonValue& actionsData)
{
    std::vector<KnownInputAction> actions;
    const JsonValue* list = actionsData.Find("actions");
    if (list == nullptr || !list->IsArray())
        return actions;

    for (const JsonValue& item : list->AsArray())
    {
        if (!item.IsObject())
            continue;
        const std::string name = ReadString(item, "name");
        if (name.empty())
            continue;

        KnownInputAction action;
        action.Name = name;

        const std::string type = ReadString(item, "type");
        if (type == "axis1")
            action.Type = InputActionType::Axis1D;
        else if (type == "axis2")
            action.Type = InputActionType::Axis2D;
        else
            action.Type = InputActionType::Digital;

        action.Scope = ReadString(item, "scope") == "presentation"
            ? InputActionScope::Presentation
            : InputActionScope::Simulation;

        actions.push_back(std::move(action));
    }
    return actions;
}

std::vector<std::string> CollectBoundActionNames(const JsonValue& profileData)
{
    std::vector<std::string> names;
    std::unordered_set<std::string> seen;

    const JsonValue* contexts = profileData.Find("contexts");
    if (contexts == nullptr || !contexts->IsArray())
        return names;

    for (const JsonValue& context : contexts->AsArray())
    {
        if (!context.IsObject())
            continue;
        const JsonValue* bindings = context.Find("bindings");
        if (bindings == nullptr || !bindings->IsArray())
            continue;

        for (const JsonValue& binding : bindings->AsArray())
        {
            if (!binding.IsObject())
                continue;
            std::string action = ReadString(binding, "action");
            if (action.empty() || !seen.insert(action).second)
                continue;
            names.push_back(std::move(action));
        }
    }
    return names;
}

std::vector<std::string> UnboundInputActions(const std::vector<KnownInputAction>& declared,
                                             const JsonValue& profileData)
{
    const std::vector<std::string> bound = CollectBoundActionNames(profileData);
    const std::unordered_set<std::string> boundSet(bound.begin(), bound.end());

    std::vector<std::string> unbound;
    for (const KnownInputAction& action : declared)
    {
        if (!boundSet.contains(action.Name))
            unbound.push_back(action.Name);
    }
    return unbound;
}

std::string DescribeInputControlName(std::string_view controlName)
{
    const std::size_t dot = controlName.find('.');
    if (dot == std::string_view::npos)
        return std::string(controlName);

    const std::string_view device = controlName.substr(0, dot);
    const std::string_view control = controlName.substr(dot + 1);
    if (control.empty())
        return std::string(controlName);

    if (device == "key")
        return TitleCaseWords(control);

    if (device == "mouse")
    {
        for (const FriendlyName& entry : kMouseNames)
        {
            if (entry.Authored == control)
                return std::string(entry.Friendly);
        }
        return "Mouse " + TitleCaseWords(control);
    }

    if (device == "gamepad")
    {
        if (control.starts_with("dpad_"))
            return "Gamepad D-pad " + TitleCaseWords(control.substr(5));
        return "Gamepad " + TitleCaseWords(control);
    }

    return std::string(controlName);
}

std::string DescribeInputBinding(const JsonValue& binding)
{
    if (!binding.IsObject())
        return "not bound yet";

    if (IsCardinal(binding))
    {
        const std::array<std::string, 4> controls = CardinalControls(binding);
        if (std::any_of(controls.begin(), controls.end(),
                        [](const std::string& c) { return c.empty(); }))
        {
            return "four directions, not finished";
        }

        // The two arrangements every author recognizes on sight get their own
        // name; anything else spells its keys out.
        if (controls == std::array<std::string, 4>{ "key.a", "key.d", "key.s", "key.w" })
            return "WASD" + DescribeConditioning(binding);
        if (controls == std::array<std::string, 4>{ "key.left", "key.right", "key.down", "key.up" })
            return "Arrow keys" + DescribeConditioning(binding);

        return JoinDescribed({ controls.begin(), controls.end() }) + DescribeConditioning(binding);
    }

    if (IsAxisPair(binding))
    {
        const std::string negative = ReadString(binding, "negative");
        const std::string positive = ReadString(binding, "positive");
        if (negative.empty() || positive.empty())
            return "two buttons, not finished";
        return JoinDescribed({ negative, positive }) + DescribeConditioning(binding);
    }

    const std::string control = ReadString(binding, "control");
    if (control.empty())
        return "not bound yet";
    return DescribeInputControlName(control) + DescribeConditioning(binding);
}

std::string InputBindingCardTitle(const JsonValue& binding)
{
    if (!binding.IsObject())
        return "Binding";
    const std::string action = ReadString(binding, "action");
    if (action.empty())
        return "Binding";
    return action + " - " + DescribeInputBinding(binding);
}

std::string InputContextCardSubtitle(const JsonValue& context)
{
    if (!context.IsObject())
        return {};

    std::string text;
    if (const JsonValue* priority = context.Find("priority");
        priority != nullptr && priority->IsNumber())
    {
        text = "priority " + FormatInputScalar(priority->AsNumber());
    }

    std::size_t count = 0;
    if (const JsonValue* bindings = context.Find("bindings");
        bindings != nullptr && bindings->IsArray())
    {
        count = bindings->AsArray().size();
    }

    if (!text.empty())
        text += " - ";
    text += std::format("{} {}", count, count == 1 ? "binding" : "bindings");
    return text;
}

std::string FormatInputScalar(double value)
{
    std::string text = std::format("{:.6f}", value);
    if (text.find('.') == std::string::npos)
        return text;
    while (!text.empty() && text.back() == '0')
        text.pop_back();
    if (!text.empty() && text.back() == '.')
        text.pop_back();
    return text.empty() ? "0" : text;
}
