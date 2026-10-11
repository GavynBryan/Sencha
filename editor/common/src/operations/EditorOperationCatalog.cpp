#include "EditorOperationCatalog.h"

#include <format>

void EditorOperationCatalogTraits::Validate(const EditorOperationDefinition& definition,
                                            std::vector<std::string>& errors)
{
    // Both roots are records so a client reads every argument and every answer
    // by name, never by position.
    if (definition.Parameters.Kind != DataFieldKind::Record)
        errors.push_back(std::format("'{}': the parameter root is a record", definition.Name));
    else
        ValidateAuthoredField(definition.Parameters, definition.Name, errors);

    if (definition.Result.Kind != DataFieldKind::Record)
        errors.push_back(std::format("'{}': the result root is a record", definition.Name));
    else
        ValidateAuthoredField(definition.Result, std::format("{} result", definition.Name), errors);
}
