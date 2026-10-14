#pragma once

#include <authored/VerbBinding.h>

#include <core/assets/AssetRef.h>
#include <core/json/JsonValue.h>

#include <string>
#include <string_view>
#include <vector>

class DataAssetTypeRegistry;
class DataSchemaRegistry;

//=============================================================================
// The `authored.bindings` structured-data subtype
//
// Where a project's bindings live. One .sdata holds a set of named records: a
// verb name, the values its arguments take, and the slots a producer fills.
//
// The compiled value is World-independent by construction. Nothing in it is a
// verb id, a tag id, an entity handle, a pointer, or a callable bound to a
// host, which is what lets one loaded asset be instantiated into the runtime
// World and an editor document at the same time and mean the right thing in
// both.
//
// The authored form, per binding:
//
//   {
//     "key": "arena.award_target",
//     "verb": "arena.award_score",
//     "inputs": ["target"],
//     "arguments": {
//       "Target": { "input": "target" },
//       "Amount": { "const": 5 },
//       "Effect": { "asset": "asset://fx/hit.smat" },
//       "Kind":   { "tag": "Score.Pickup" },
//       "Anchor": { "entity": "00000000000000ab" }
//     }
//   }
//
// Every argument is one wrapper object with exactly one source key. The wrapper
// is not ceremony: a record-valued constant is itself an object, so an
// unwrapped form could not tell a literal from an input reference, and an
// explicit reference kind is what lets the staging pass name this binding's
// asset dependencies without a World to resolve them against.
//=============================================================================

inline constexpr std::string_view kVerbBindingsTypeName = "authored.bindings";

void RegisterVerbBindingData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas);
void UnregisterVerbBindingData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas);

// One argument's wrapper object, as above. An asset or data-asset reference is
// appended to `dependencies`; false leaves `error` naming the binding and argument.
[[nodiscard]] bool ParseVerbBindingArgument(std::string_view bindingKey,
                                            const std::string& argumentKey,
                                            const JsonValue& value,
                                            VerbBindingArgument& out,
                                            std::vector<AssetRef>& dependencies,
                                            std::string& error);
