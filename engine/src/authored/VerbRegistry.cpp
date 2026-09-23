#include <authored/VerbRegistry.h>

#include <format>

void VerbCatalogTraits::Validate(const VerbDefinition& definition, std::vector<std::string>& errors)
{
    if (definition.Arguments.Kind != DataFieldKind::Record)
        errors.push_back(std::format("'{}': the argument root is a record", definition.Name));
    else
        ValidateAuthoredField(definition.Arguments, definition.Name, errors);
}
