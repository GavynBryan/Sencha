#pragma once

#include "documents/DocumentRef.h"
#include "problems/ProblemList.h"

#include <abilities/AbilityDeclaration.h>
#include <core/json/JsonValue.h>

#include <string_view>
#include <vector>

class AssetRegistry;
class World;

// What an ability's working data means against the names `names` declares:
// compiled, and resolved once it compiles. `name` is the asset's path.
[[nodiscard]] std::vector<AbilityDiagnostic> CheckAbilityData(const JsonValue& data, std::string_view name,
                                                              const World& names);
// The same, keeping what the data compiled to. Given the mounted assets, an asset
// an operation names is checked to be there and of the kind its verb takes, as the
// runtime checks it when the content is declared.
[[nodiscard]] std::vector<AbilityDiagnostic> CheckAbilityData(const JsonValue& data, std::string_view name,
                                                              const World& names, AbilityDeclaration& compiled,
                                                              const AssetRegistry* assets = nullptr);

// Each diagnostic as a problem in `document`, named by the element it is about
// and located by its path, which a reveal turns back into the element.
[[nodiscard]] std::vector<Problem> AbilityProblems(const std::vector<AbilityDiagnostic>& diagnostics,
                                                   const JsonValue& data, const DocumentRef& document);
