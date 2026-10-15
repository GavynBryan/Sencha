#pragma once

#include <abilities/AbilityDeclaration.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <core/metadata/DataSchema.h>

#include <string>
#include <string_view>
#include <vector>

inline constexpr std::string_view kAbilityDefinitionType = "ability.definition";

void RegisterAbilityDefinitionData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas);

// Reads `data` into a declaration and checks everything that needs no World:
// keys, references between elements, reachability, zero-time cycles, cost and
// commit placement, guards on values that may be empty, capacities. True when
// nothing found is an error. Effect and asset references are appended to
// `dependencies`.
[[nodiscard]] bool CompileAbilityDeclaration(const JsonValue& data,
                                             AbilityDeclaration& out,
                                             std::vector<AssetRef>& dependencies,
                                             std::vector<AbilityDiagnostic>& diagnostics);

// Grant entries -- { key, ability: { "data": ... }, binding? } -- wherever content
// states them outside a scene: an effect's granted_abilities. True when none is
// malformed; ability references are appended to `dependencies`.
[[nodiscard]] bool ParseAbilityGrantEntries(const JsonValue& list,
                                            const std::string& path,
                                            std::vector<AbilityGrantDeclaration>& out,
                                            std::vector<AssetRef>& dependencies,
                                            std::vector<AbilityDiagnostic>& diagnostics);
[[nodiscard]] DataFieldSchema AbilityGrantEntriesSchema(std::string key, std::string label);
