#pragma once

#include <world/serialization/IComponentSerializer.h>

#include <memory>

// The scene form of AbilityGrants, which names abilities by asset and grants by
// stable key, since the ids are this World's registration order:
//
//   "AbilityGrants": { "grants": [ { "key": "g_primary", "ability": <asset ref>,
//                                    "binding": { "action": "Input.AltFire", ... } } ] }
//
// Authored grants and those an ability.grant request made are saved, and load as
// authored; a grant an effect holds comes back with its effect. JSON only.
[[nodiscard]] std::unique_ptr<IComponentSerializer> MakeAbilityGrantsSerializer();
