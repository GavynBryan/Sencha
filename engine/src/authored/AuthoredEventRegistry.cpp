#include <authored/AuthoredEventRegistry.h>

#include <algorithm>
#include <format>

void AuthoredEventCatalogTraits::Validate(const AuthoredEventDefinition& definition,
                                          std::vector<std::string>& errors)
{
    if (definition.Payload.Kind != DataFieldKind::Record)
        errors.push_back(std::format("'{}': the payload root is a record", definition.Name));
    else
        ValidateAuthoredField(definition.Payload, definition.Name, errors);
    if (definition.Instigator.empty())
        return;
    const auto& fields = definition.Payload.Children;
    const auto instigator = std::ranges::find(fields, definition.Instigator, &DataFieldSchema::Key);
    if (instigator == fields.end() || instigator->Kind != DataFieldKind::Entity)
        errors.push_back(std::format("'{}': the instigator '{}' is not an entity field of the payload",
                                     definition.Name, definition.Instigator));
}
