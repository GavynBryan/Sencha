#include "authoring/AbilityStrip.h"

#include "authoring/AbilityConditionText.h"
#include "authoring/AbilityDocumentEdits.h"
#include "authoring/AbilityElements.h"
#include "data/DataPath.h"
#include "data/JsonObjectEdit.h"

#include <array>
#include <format>
#include <limits>
#include <map>

namespace
{
constexpr std::string_view kSlotPrefix = "slot:";

struct SlotTitle
{
    AbilityOpList List;
    std::string_view Title;
};

constexpr std::array<SlotTitle, 4> kSlots{ {
    { AbilityOpList::Enter, "ON ENTRY" },
    { AbilityOpList::While, "REPEATING" },
    { AbilityOpList::Hold, "HELD" },
    { AbilityOpList::Exit, "ON EXIT" },
} };

std::string_view SeverityName(AbilityDiagnosticSeverity severity)
{
    switch (severity)
    {
    case AbilityDiagnosticSeverity::Error: return "error";
    case AbilityDiagnosticSeverity::Warning: return "warning";
    case AbilityDiagnosticSeverity::Info: return "info";
    }
    return {};
}

// The worst severity reported against each element and each phase holding one.
std::map<std::string, AbilityDiagnosticSeverity, std::less<>> WorstOf(const JsonValue& data,
                                                                       std::span<const AbilityDiagnostic> diagnostics)
{
    std::map<std::string, AbilityDiagnosticSeverity, std::less<>> worst;
    const auto note = [&](const std::string& key, AbilityDiagnosticSeverity severity) {
        const auto [at, inserted] = worst.emplace(key, severity);
        if (!inserted && severity < at->second)
            at->second = severity;
    };
    for (const AbilityDiagnostic& diagnostic : diagnostics)
    {
        std::optional<AbilityElement> element =
            diagnostic.ElementKey.empty() ? std::nullopt : FindAbilityElement(data, diagnostic.ElementKey);
        if (!element)
            element = AbilityElementAt(data, diagnostic.Path);
        if (!element)
            continue;
        note(element->Key, diagnostic.Severity);
        if (!element->Phase.empty())
            note(element->Phase, diagnostic.Severity);
    }
    return worst;
}

std::string GuardOf(const JsonValue& data, const JsonValue& element)
{
    const JsonValue* when = FindMember(element, "when");
    return when != nullptr ? AbilityConditionText(data, *when) : std::string{};
}

std::string KindOf(const JsonValue& op)
{
    if (FindMember(op, "lease") != nullptr)
        return "LEASE";
    const std::string kind = ReadMemberString(op, "op");
    if (kind == "invoke")
        return "VERB";
    std::string upper = kind;
    for (char& c : upper)
        c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
    return upper;
}

std::string ClockOf(const JsonValue& phase)
{
    const JsonValue* timing = FindMember(phase, "timing");
    const std::string domain = timing != nullptr ? ReadMemberString(*timing, "domain") : std::string{};
    const JsonValue* progress = timing != nullptr ? FindMember(*timing, "progress_ticks") : nullptr;
    std::string text = domain == "simulation" ? "simulation time" : "actor time";
    if (progress != nullptr && progress->IsNumber())
        text += std::format(" \xC2\xB7 full in {}", static_cast<long long>(progress->AsNumber()));
    return text;
}

std::optional<AbilityOpList> OpListOf(const AbilityElement& op)
{
    return op.List == "ops" ? std::optional(AbilityOpList::While) : AbilityOpListNamed(op.List);
}

UiRow Row(std::string key, std::string label, std::string value, std::string detail = {})
{
    UiRow row{ std::move(label), std::move(value), std::move(detail) };
    row.Key = std::move(key);
    return row;
}

void Mark(UiRow& row, const std::map<std::string, AbilityDiagnosticSeverity, std::less<>>& worst, std::string guard)
{
    const auto found = worst.find(row.Key);
    row.Parts = { std::move(guard),
                  found != worst.end() ? std::string(SeverityName(found->second)) : std::string{} };
}
} // namespace

std::string AbilityStripSlotKey(std::string_view phase, std::string_view list)
{
    return std::format("{}{}:{}", kSlotPrefix, phase, list);
}

AbilityStripRows AbilityStripOf(const JsonValue& data, std::span<const AbilityDiagnostic> diagnostics,
                                std::string_view selected)
{
    const std::vector<AbilityElement> elements = AbilityElementsOf(data);
    const auto worst = WorstOf(data, diagnostics);
    std::string selectedPhase;
    for (const AbilityElement& element : elements)
        if (element.Key == selected)
            selectedPhase = element.Kind == AbilityElementKind::Phase ? element.Key : element.Phase;

    AbilityStripRows rows;
    for (const AbilityElement& phase : elements)
    {
        if (phase.Kind != AbilityElementKind::Phase)
            continue;
        const std::size_t into = AbilityTransitionsInto(data, phase.Key);
        UiRow card = Row(phase.Key, AbilityElementLabel(data, phase), {}, ClockOf(*FindDataValue(data, phase.Path)));
        if (into > 0)
            card.Detail += std::format(" \xC2\xB7 {} in", into);
        card.Selected = phase.Key == selected;
        card.Expanded = phase.Key == selectedPhase;
        Mark(card, worst, {});
        rows.Cards.push_back(std::move(card));
        for (const SlotTitle& slot : kSlots)
            rows.Slots.push_back(Row(AbilityStripSlotKey(phase.Key, AbilityOpListName(slot.List)), std::string(slot.Title),
                                     phase.Key));
    }
    for (const AbilityElement& element : elements)
    {
        const JsonValue* value = FindDataValue(data, element.Path);
        if (value == nullptr)
            continue;
        if (element.Kind == AbilityElementKind::Op)
        {
            const std::optional<AbilityOpList> list = OpListOf(element);
            UiRow op = Row(element.Key, AbilityElementLabel(data, element),
                           list ? AbilityStripSlotKey(element.Phase, AbilityOpListName(*list)) : std::string{},
                           KindOf(*value));
            op.Selected = element.Key == selected;
            Mark(op, worst, GuardOf(data, *value));
            rows.Ops.push_back(std::move(op));
        }
        else if (element.Kind == AbilityElementKind::Transition)
        {
            UiRow link = Row(element.Key, AbilityElementLabel(data, element), element.Phase);
            link.Selected = element.Key == selected;
            Mark(link, worst, GuardOf(data, *value));
            rows.Links.push_back(std::move(link));
        }
    }
    return rows;
}

bool DropAbilityElement(JsonValue& data, std::string_view carried, std::string_view target)
{
    const std::optional<AbilityElement> moving = FindAbilityElement(data, carried);
    if (!moving || carried == target)
        return false;
    constexpr std::size_t kEnd = std::numeric_limits<std::size_t>::max();
    if (target.starts_with(kSlotPrefix))
    {
        const std::string_view rest = target.substr(kSlotPrefix.size());
        const std::size_t colon = rest.rfind(':');
        const std::optional<AbilityOpList> list =
            colon == std::string_view::npos ? std::nullopt : AbilityOpListNamed(rest.substr(colon + 1));
        return moving->Kind == AbilityElementKind::Op && list
            && MoveAbilityOp(data, carried, rest.substr(0, colon), *list, kEnd);
    }
    const std::optional<AbilityElement> onto = FindAbilityElement(data, target);
    if (!onto)
        return false;
    if (moving->Kind == AbilityElementKind::Phase && onto->Kind == AbilityElementKind::Phase)
    {
        const std::size_t before = moving->Index < onto->Index ? onto->Index - 1 : onto->Index;
        return MoveAbilityPhase(data, carried, before);
    }
    if (moving->Kind != AbilityElementKind::Op)
        return false;
    if (onto->Kind == AbilityElementKind::Phase)
        return MoveAbilityOp(data, carried, onto->Key, AbilityOpList::Enter, kEnd);
    const std::optional<AbilityOpList> list = onto->Kind == AbilityElementKind::Op ? OpListOf(*onto) : std::nullopt;
    if (!list)
        return false;
    const bool sameList = moving->Phase == onto->Phase && moving->List == onto->List;
    const std::size_t before = sameList && moving->Index < onto->Index ? onto->Index - 1 : onto->Index;
    return MoveAbilityOp(data, carried, onto->Phase, *list, before);
}
