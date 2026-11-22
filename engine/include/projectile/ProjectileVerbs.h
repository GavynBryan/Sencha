#pragma once

#include <authored/AuthoredEventRegistry.h>
#include <authored/VerbInvocation.h>
#include <authored/VerbRegistry.h>

#include <cstdint>
#include <string_view>

class ProjectileSystem;
enum class ProjectileLaunchResult : std::uint8_t;

inline constexpr std::string_view kProjectileLaunchVerb = "projectile.launch";

// projectile.launch and the projectile.hit occurrence.
void DeclareProjectileVocabulary(VerbRegistrationScope& verbs, AuthoredEventRegistrationScope& events);

// Arguments, in order: instigator, projectile (the prefab's scene path), origin
// and direction (both optional: the instigator's aim), spread in degrees, speed
// scale, magnitude, splash scale. Accepted when it launched; QueueFull at
// capacity; UnresolvedReference for a prefab that is not a projectile.
VerbAdmission InvokeProjectileLaunch(ProjectileSystem& system, const VerbInvocation& invocation);

// What a verb that launches answers for a launch's result.
[[nodiscard]] VerbAdmission ProjectileLaunchAdmission(ProjectileLaunchResult result);
