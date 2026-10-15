#include "authoring/AbilityOverview.h"

#include "authoring/AbilityConditionText.h"
#include "authoring/AbilityWorkbench.h"
#include "data/JsonObjectEdit.h"
#include "properties/SettingProperties.h"
#include "text/PathLeaf.h"

#include <abilities/AbilityBindingNames.h>
#include <abilities/AbilityGrantEntries.h>

#include <format>

namespace
{
std::string Conditions(const JsonValue& data, std::string_view list, std::string_view none)
{
    std::string text;
    if (const JsonValue* conditions = FindMember(data, list); conditions != nullptr && conditions->IsArray())
        for (const JsonValue& condition : conditions->AsArray())
            text += std::format("{}{}", text.empty() ? "" : "; ", AbilityConditionText(data, condition));
    return text.empty() ? std::string(none) : text;
}

std::string Binding(const AbilityDeclaration& declaration)
{
    const AbilityBindingDecl binding = EffectiveAbilityBinding({}, declaration.InputDefault);
    if (!binding.Action)
        return "by request only";
    return std::format("{} \xC2\xB7 {} \xC2\xB7 priority {} \xC2\xB7 {} \xC2\xB7 {} when blocked \xC2\xB7 {} on interrupt",
                       *binding.Action, AbilityNameOf(kAbilityInputPolicyNames, *binding.Policy), *binding.Priority,
                       AbilityNameOf(kAbilityConsumeNames, *binding.Consume),
                       AbilityNameOf(kAbilityWhenBlockedNames, *binding.WhenBlocked),
                       AbilityNameOf(kAbilityInterruptNames, *binding.OnInterrupt));
}

std::string EffectsOf(const JsonValue& holder)
{
    std::string text;
    if (const JsonValue* effects = FindMember(holder, "effects"); effects != nullptr && effects->IsArray())
        for (const JsonValue& effect : effects->AsArray())
            text += std::format("{}{}", text.empty() ? "" : ", ", PathLeaf(ReadMemberString(effect, "data")));
    return text;
}

std::string Costs(const JsonValue& data)
{
    std::string text;
    if (const JsonValue* costs = FindMember(data, "costs"); costs != nullptr && costs->IsArray())
        for (const JsonValue& cost : costs->AsArray())
        {
            const std::string name = ReadMemberString(cost, "name").empty() ? ReadMemberString(cost, "key")
                                                                             : ReadMemberString(cost, "name");
            const std::string when = ReadMemberString(cost, "when").empty() ? "commit" : ReadMemberString(cost, "when");
            const std::string repeat =
                ReadMemberString(cost, "repeat").empty() ? "once" : ReadMemberString(cost, "repeat");
            text += std::format("{}{}: on {}, {} ({})", text.empty() ? "" : "; ", name, when, repeat, EffectsOf(cost));
        }
    return text.empty() ? std::string("none") : text;
}

std::string Cooldown(const JsonValue& data)
{
    const JsonValue* cooldown = FindMember(data, "cooldown");
    if (cooldown == nullptr)
        return "none";
    const JsonValue* effect = FindMember(*cooldown, "effect");
    const std::string start = ReadMemberString(*cooldown, "start").empty() ? "on_end" : ReadMemberString(*cooldown, "start");
    return std::format("{} \xC2\xB7 {}", effect != nullptr ? std::string(PathLeaf(ReadMemberString(*effect, "data")))
                                                          : std::string("?"),
                       start);
}

std::string Problems(const std::vector<AbilityDiagnostic>& diagnostics)
{
    std::size_t errors = 0;
    std::size_t warnings = 0;
    for (const AbilityDiagnostic& diagnostic : diagnostics)
    {
        errors += diagnostic.Severity == AbilityDiagnosticSeverity::Error ? 1 : 0;
        warnings += diagnostic.Severity == AbilityDiagnosticSeverity::Warning ? 1 : 0;
    }
    if (errors + warnings == 0)
        return "none";
    return std::format("{} error{}, {} warning{}", errors, errors == 1 ? "" : "s", warnings, warnings == 1 ? "" : "s");
}
} // namespace

std::vector<PropertyNode> AbilityOverviewProperties::Nodes() const
{
    const JsonValue* data = Workbench.AbilityData();
    if (data == nullptr)
        return {};
    const std::string prediction =
        ReadMemberString(*data, "prediction").empty() ? "presentation" : ReadMemberString(*data, "prediction");
    return { NoteProperty("overview.input", "Input", Binding(Workbench.Declaration())),
             NoteProperty("overview.activation", "Starts only if", Conditions(*data, "activation", "always")),
             NoteProperty("overview.continuation", "Continues while", Conditions(*data, "continuation", "always")),
             NoteProperty("overview.cancel_on", "Cancelled when", Conditions(*data, "cancel_on", "never")),
             NoteProperty("overview.costs", "Costs", Costs(*data)),
             NoteProperty("overview.cooldown", "Cooldown", Cooldown(*data)),
             NoteProperty("overview.prediction", "Prediction", prediction),
             NoteProperty("overview.problems", "Problems", Problems(Workbench.Diagnostics())) };
}
