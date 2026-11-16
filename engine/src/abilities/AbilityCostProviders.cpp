#include <abilities/AbilityCostProviders.h>

#include <format>

void AbilityCostProviderCatalogTraits::Validate(const AbilityCostProviderDefinition& definition,
                                                std::vector<std::string>& errors)
{
    const bool owns = definition.Check != nullptr && definition.Apply != nullptr;
    const bool spendsThrough = !definition.SpendsThrough.empty() && definition.Translate != nullptr;
    const bool mixed = (definition.Check != nullptr || definition.Apply != nullptr)
        && (!definition.SpendsThrough.empty() || definition.Translate != nullptr);
    if (owns == spendsThrough || mixed)
        errors.push_back(std::format("cost provider '{}' needs a check and an apply, or a provider it spends "
                                     "through and a translation, not both",
                                     definition.Name));
    if (definition.SpendsThrough == definition.Name)
        errors.push_back(std::format("cost provider '{}' cannot spend through itself", definition.Name));
    if (definition.Arguments.Kind != DataFieldKind::Record)
        errors.push_back(std::format("cost provider '{}' takes a record of arguments", definition.Name));
}
