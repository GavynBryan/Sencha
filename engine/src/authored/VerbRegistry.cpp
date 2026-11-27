#include <authored/VerbRegistry.h>

#include <algorithm>
#include <format>

void VerbCatalogTraits::Validate(const VerbDefinition& definition, std::vector<std::string>& errors)
{
    if (definition.Arguments.Kind != DataFieldKind::Record)
        errors.push_back(std::format("'{}': the argument root is a record", definition.Name));
    else
        ValidateAuthoredField(definition.Arguments, definition.Name, errors);
    const bool cosmetic = definition.Scope == AuthoredScope::Cosmetic;
    if (cosmetic && definition.Prediction == AuthoredPrediction::Simulation)
        errors.push_back(std::format("'{}': a cosmetic verb changes no simulation state to predict", definition.Name));
    if (!cosmetic && definition.Prediction == AuthoredPrediction::Presentation)
        errors.push_back(std::format("'{}': a presentation-predicted verb is cosmetic", definition.Name));
    if (definition.Completes && definition.Kind != VerbKind::Lease)
        errors.push_back(std::format("'{}': only a lease is held long enough to complete", definition.Name));

    for (std::size_t i = 0; i < definition.Refusals.size(); ++i)
    {
        const VerbRefusalReason& reason = definition.Refusals[i];
        if (reason.Code == 0)
            errors.push_back(
                std::format("'{}': refusal '{}' has code 0, which names no reason", definition.Name, reason.Name));
        if (!IsValidAuthoredName(reason.Name))
            errors.push_back(std::format("'{}': refusal name '{}' is not a valid name", definition.Name, reason.Name));
        for (std::size_t j = 0; j < i; ++j)
            if (definition.Refusals[j].Code == reason.Code || definition.Refusals[j].Name == reason.Name)
                errors.push_back(
                    std::format("'{}': refusal '{}' repeats a code or a name", definition.Name, reason.Name));
    }
    if (definition.Result.has_value())
    {
        ValidateAuthoredField(*definition.Result, std::format("{} result", definition.Name), errors);
        if (definition.Kind == VerbKind::Lease)
            errors.push_back(std::format("'{}': a lease answers no result", definition.Name));
    }
}

bool VerbCatalogTraits::RefusalsMatch(const std::vector<VerbRefusalReason>& left,
                                      const std::vector<VerbRefusalReason>& right)
{
    return std::ranges::equal(left, right, [](const VerbRefusalReason& a, const VerbRefusalReason& b) {
        return a.Code == b.Code && a.Name == b.Name;
    });
}

bool VerbCatalogTraits::ResultsMatch(const std::optional<DataFieldSchema>& left,
                                     const std::optional<DataFieldSchema>& right)
{
    if (left.has_value() != right.has_value())
        return false;
    return !left.has_value() || AuthoredContractsMatch(*left, *right);
}

std::string_view VerbRefusalName(const VerbRegistry& verbs, VerbId verb, std::uint16_t code)
{
    const VerbDefinition* definition = verbs.Get(verb);
    if (definition == nullptr || code == 0)
        return {};
    const auto found = std::ranges::find(definition->Refusals, code, &VerbRefusalReason::Code);
    return found != definition->Refusals.end() ? std::string_view(found->Name) : std::string_view();
}

std::string DescribeVerbAnswer(const VerbRegistry& verbs, VerbId verb, VerbAnswer answer)
{
    const VerbDefinition* definition = verbs.Get(verb);
    switch (answer.Status)
    {
    case VerbAdmission::Refused:
    {
        if (answer.Detail == 0)
            break;
        const std::string_view name = VerbRefusalName(verbs, verb, answer.Detail);
        return name.empty() ? std::format("refused: {}", answer.Detail) : std::format("refused: {}", name);
    }
    case VerbAdmission::InvalidArguments:
    case VerbAdmission::UnresolvedReference:
    {
        if (answer.Detail == 0)
            break;
        const char* what = answer.Status == VerbAdmission::InvalidArguments ? "invalid argument" : "unresolved reference";
        const std::size_t slot = answer.Detail - 1u;
        if (definition != nullptr && slot < definition->Arguments.Children.size())
            return std::format("{} '{}'", what, definition->Arguments.Children[slot].Key);
        return std::format("{} {}", what, answer.Detail);
    }
    default:
        break;
    }
    return VerbAdmissionName(answer.Status);
}
