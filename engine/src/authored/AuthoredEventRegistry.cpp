#include <authored/AuthoredEventRegistry.h>

#include <format>

void AuthoredEventCatalogTraits::Validate(const AuthoredEventDefinition& definition,
                                          std::vector<std::string>& errors)
{
    if (definition.Payload.Kind != DataFieldKind::Record)
        errors.push_back(std::format("'{}': the payload root is a record", definition.Name));
    else
        ValidateAuthoredField(definition.Payload, definition.Name, errors);
}
