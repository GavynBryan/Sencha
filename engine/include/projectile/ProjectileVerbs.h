#pragma once

#include <authored/AuthoredAnnotations.h>
#include <authored/AuthoredApiDefinition.h>
#include <authored/AuthoredEventRegistry.h>
#include <authored/VerbInvocation.h>
#include <authored/VerbRegistry.h>
#include <core/assets/AssetRef.h>
#include <ecs/EntityId.h>
#include <math/Vec.h>

#include <cstdint>
#include <optional>

class ProjectileSystem;
enum class ProjectileLaunchResult : std::uint8_t;

// projectile.launch and the projectile.hit occurrence.
void DeclareProjectileVocabulary(VerbRegistrationScope& verbs, AuthoredEventRegistrationScope& events);

// What a verb that launches answers for a launch's result.
[[nodiscard]] VerbAdmission ProjectileLaunchAdmission(ProjectileLaunchResult result);

class ProjectileOperations
{
public:
    explicit ProjectileOperations(ProjectileSystem& system)
        : System(&system)
    {
    }

    SENCHA_VERB("projectile.launch")
    SENCHA_LABEL("Launch projectile")
    SENCHA_CATEGORY("Projectiles")
    SENCHA_DESCRIPTION("Launches a projectile prefab from the instigator's aim, or from an origin along a direction. "
                       "The authority flies it and decides what it strikes.")
    VerbAdmission Launch(
        const VerbInvocation& invocation,
        SENCHA_ARG("instigator") SENCHA_LABEL("Instigator")
            SENCHA_DESCRIPTION("Who launched it: never struck by it, and credited with what it strikes.")
                EntityId instigator,
        SENCHA_ARG("projectile") SENCHA_LABEL("Projectile") SENCHA_ASSET(Scene)
            SENCHA_DESCRIPTION("The prefab: a scene whose root carries Projectile. Content that names it holds it "
                               "resident.") const AssetRef& projectile,
        SENCHA_ARG("origin") SENCHA_LABEL("Origin") SENCHA_DESCRIPTION("Where it starts. The instigator's aim when absent.")
            std::optional<Vec3d> origin = std::nullopt,
        SENCHA_ARG("direction") SENCHA_LABEL("Direction")
            SENCHA_DESCRIPTION("Which way it goes. The instigator's aim when absent.") SENCHA_WIDGET("direction")
                std::optional<Vec3d> direction = std::nullopt,
        SENCHA_ARG("spread") SENCHA_LABEL("Spread")
            SENCHA_DESCRIPTION("Degrees either side of the direction it may scatter by.") SENCHA_MIN(0)
                float spread = 0.0f,
        SENCHA_ARG("speed_scale") SENCHA_LABEL("Speed scale") SENCHA_DESCRIPTION("Multiplies the projectile's own speed.")
            SENCHA_MIN(0) float speedScale = 1.0f,
        SENCHA_ARG("magnitude") SENCHA_LABEL("Magnitude")
            SENCHA_DESCRIPTION("What the launcher measured -- a charge, a distance -- scaling the projectile's scaled "
                               "effect. Zero applies none.") SENCHA_MIN(0) float magnitude = 0.0f,
        SENCHA_ARG("splash_scale") SENCHA_LABEL("Splash scale")
            SENCHA_DESCRIPTION("Multiplies the projectile's splash radius; zero has none.") SENCHA_MIN(0)
                float splashScale = 1.0f);

private:
    ProjectileSystem* System;
};

#if !defined(SENCHA_CODEGEN)
#  include <projectile/ProjectileVerbs.sencha.h>
#endif
