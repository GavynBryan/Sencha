#include <authored/AuthoredQueryRegistry.h>

#include <format>

void AuthoredQueryCatalogTraits::Validate(const AuthoredQueryDefinition& definition,
                                          std::vector<std::string>& errors)
{
    if (definition.Arguments.Kind != DataFieldKind::Record)
        errors.push_back(std::format("'{}': the argument root is a record", definition.Name));
    else
        ValidateAuthoredField(definition.Arguments, definition.Name, errors);

    if (definition.Result.Kind == DataFieldKind::Optional)
    {
        errors.push_back(std::format(
            "'{}': a result is never optional; a query with no answer reports Unavailable",
            definition.Name));
    }
    else
    {
        ValidateAuthoredField(definition.Result, std::format("{} result", definition.Name), errors);
    }
}
