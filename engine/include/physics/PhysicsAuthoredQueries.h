#pragma once

#include <authored/AuthoredQueryRegistry.h>
#include <authored/AuthoredQueryStatus.h>
#include <authored/AuthoredValue.h>

#include <span>
#include <string_view>

class PhysicsQueries;

// The closest body along a ray: {hit, entity, point, normal}. `entity` may be invalid
// on a hit, for a body no entity owns.
inline constexpr std::string_view kPhysicsTraceQuery = "physics.trace";
// Every entity whose body overlaps a sphere.
inline constexpr std::string_view kPhysicsOverlapQuery = "physics.overlap";

void DeclarePhysicsQueries(AuthoredQueryRegistrationScope& scope);

AuthoredQueryStatus EvaluatePhysicsTrace(const PhysicsQueries& physics,
                                         std::span<const AuthoredValue> arguments,
                                         AuthoredValue& result);
AuthoredQueryStatus EvaluatePhysicsOverlap(const PhysicsQueries& physics,
                                           std::span<const AuthoredValue> arguments,
                                           AuthoredValue& result);
