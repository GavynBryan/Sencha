#include <authored/VerbRegistry.h>

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
}
