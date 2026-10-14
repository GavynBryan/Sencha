#pragma once

#include "documents/DocumentRef.h"
#include "problems/ProblemList.h"

#include <abilities/AbilityDeclaration.h>
#include <core/json/JsonValue.h>

#include <string_view>
#include <vector>

class World;

// What an ability's working data means against the names `names` declares:
// compiled, and resolved once it compiles. `name` is the asset's path.
[[nodiscard]] std::vector<AbilityDiagnostic> CheckAbilityData(const JsonValue& data, std::string_view name,
                                                              const World& names);

// Each diagnostic as a problem in `document`, named by the element it is about
// and located by its path, which a reveal turns back into the element.
[[nodiscard]] std::vector<Problem> AbilityProblems(const std::vector<AbilityDiagnostic>& diagnostics,
                                                   const JsonValue& data, const DocumentRef& document);
