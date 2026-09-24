#pragma once

#include <assets/data/DataAssetTypeRegistry.h>
#include <core/metadata/DataSchema.h>
#include <navigation/NavigationTypes.h>

#include <string>
#include <string_view>
#include <vector>

class GameplayTagRegistry;

//=============================================================================
// navigation.policy data assets
//
// How an agent weighs ground and traversals, authored as data so a policy
// change reroutes agents without recompiling or recooking anything:
//
//   { "type": "navigation.policy", "version": 1,
//     "data": {
//       "area_costs": [ { "area": "navigation.area.water", "cost": 1.4 } ],
//       "forbidden_areas": [ "navigation.area.hazard" ],
//       "traversal_costs": [ { "kind": "navigation.traversal.jump",
//                              "multiplier": 2.0, "add": 1.0 } ] } }
//
// The compiled value keeps names; BindNavigationPolicy resolves them against
// the gameplay tags the running game registered.
//=============================================================================

inline constexpr std::string_view kNavigationPolicySubtype = "navigation.policy";

struct NavigationPolicyData
{
    struct AreaCost
    {
        std::string Area;
        float Cost = 1.0f;
    };
    struct TraversalCost
    {
        std::string Kind;
        float Multiplier = 1.0f;
        float Add = 0.0f;
    };

    std::vector<AreaCost> AreaCosts;
    std::vector<std::string> ForbiddenAreas;
    std::vector<TraversalCost> TraversalCosts;
};

[[nodiscard]] const DataSchema& NavigationPolicySchema();

// Validates `data` against NavigationPolicySchema, then compiles it.
[[nodiscard]] DataAssetCompileResult CompileNavigationPolicy(const JsonValue& data);

// Resolves names to tag ids. A name no code registered is skipped and, when
// `unresolved` is given, reported there -- never silently treated as another tag.
[[nodiscard]] NavQueryPolicy BindNavigationPolicy(const NavigationPolicyData& data,
                                                  const GameplayTagRegistry& tags,
                                                  std::vector<std::string>* unresolved = nullptr);

void RegisterNavigationPolicyData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas);
