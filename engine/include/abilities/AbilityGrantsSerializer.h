#pragma once

#include <world/serialization/IComponentSerializer.h>

#include <memory>

// The scene form of AbilityGrants, which names abilities by asset and grants by
// stable key, since the ids are this World's registration order:
//
//   "AbilityGrants": { "grants": [ { "key": "g_primary", "ability": <asset ref>,
//                                    "binding": { "action": "Input.AltFire", ... } } ] }
//
// Only authored grants are saved; grants an effect or a request made are run-time
// facts. JSON only, like every scene field that names an asset.
[[nodiscard]] std::unique_ptr<IComponentSerializer> MakeAbilityGrantsSerializer();
