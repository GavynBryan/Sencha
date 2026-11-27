#pragma once

#include <anim/AnimTypes.h>
#include <authored/AuthoredAnnotations.h>
#include <authored/AuthoredApiDefinition.h>
#include <authored/VerbInvocation.h>
#include <core/metadata/EnumSchema.h>
#include <ecs/EntityId.h>
#include <gameplay_tags/GameplayTagId.h>

#include <array>
#include <cstdint>

class FixedSimulationLoop;
class World;

// The lifetimes a presentation-only request can have: one with no simulation behind it
// cannot be held.
enum class AnimPresentLifetime : std::uint8_t
{
    Impulse,
    Fixed,
};

template <>
struct EnumSchema<AnimPresentLifetime>
{
    static constexpr std::array Values = {
        EnumValue{ AnimPresentLifetime::Impulse, "impulse", "Impulse" },
        EnumValue{ AnimPresentLifetime::Fixed, "fixed", "Fixed" },
    };
};

// The authored API's door to animation requests, for producers with no ability to
// own one: level logic, scripted gameplay, AI, props. Every request goes through
// RequestAnimation and CancelAnimation, the one path every producer uses.
class AnimRequestOperations
{
public:
    AnimRequestOperations(World& world, const FixedSimulationLoop& clock)
        : Entities(&world)
        , Clock(&clock)
    {
    }

    SENCHA_VERB("anim.request")
    SENCHA_LABEL("Request animation")
    SENCHA_CATEGORY("Animation")
    SENCHA_DESCRIPTION("Asks an animated entity to present an intent its rig declares.")
    SENCHA_PREDICTION(Simulation)
    VerbAdmission Request(
        const VerbInvocation& invocation,
        SENCHA_TARGET("target") SENCHA_LABEL("Target") SENCHA_DESCRIPTION("The animated entity.") EntityId target,
        SENCHA_ARG("intent") SENCHA_LABEL("Intent") SENCHA_DESCRIPTION("What to present, as its rig names it.")
            GameplayTagId intent,
        SENCHA_ARG("lifetime") SENCHA_LABEL("Lifetime")
            SENCHA_DESCRIPTION("Held lasts until anim.cancel; fixed for `ticks`; impulse for one tick.")
                AnimRequestLifetime lifetime = AnimRequestLifetime::Held,
        SENCHA_ARG("ticks") SENCHA_LABEL("Ticks") SENCHA_DESCRIPTION("How long a fixed request lasts.")
            std::uint32_t ticks = 0);

    SENCHA_VERB("anim.cancel")
    SENCHA_LABEL("End animation request")
    SENCHA_CATEGORY("Animation")
    SENCHA_DESCRIPTION("Ends the held request this producer asked the entity for.")
    SENCHA_PREDICTION(Simulation)
    VerbAdmission Cancel(
        const VerbInvocation& invocation,
        SENCHA_TARGET("target") SENCHA_LABEL("Target") SENCHA_DESCRIPTION("The animated entity.") EntityId target,
        SENCHA_ARG("intent") SENCHA_LABEL("Intent") SENCHA_DESCRIPTION("The intent it was asked for.")
            GameplayTagId intent);

    SENCHA_LEASE("anim.hold")
    SENCHA_LABEL("Hold animation")
    SENCHA_CATEGORY("Animation")
    SENCHA_DESCRIPTION("Keeps an animated entity presenting an intent until its holder lets go.")
    SENCHA_PREDICTION(Simulation)
    VerbAdmission Hold(
        const VerbInvocation& invocation,
        SENCHA_TARGET("target") SENCHA_LABEL("Target") SENCHA_DESCRIPTION("The animated entity.") EntityId target,
        SENCHA_ARG("intent") SENCHA_LABEL("Intent") SENCHA_DESCRIPTION("What to present, as its rig names it.")
            GameplayTagId intent);
    SENCHA_LEASE_RELEASE("anim.hold")
    VerbAdmission EndHold(const VerbInvocation& invocation);

    SENCHA_VERB("anim.present")
    SENCHA_LABEL("Present animation")
    SENCHA_CATEGORY("Animation")
    SENCHA_DESCRIPTION("Plays an intent on the entity's presentation layers, on this machine alone.")
    SENCHA_COSMETIC
    VerbAdmission Present(
        const VerbInvocation& invocation,
        SENCHA_TARGET("target") SENCHA_LABEL("Target") SENCHA_DESCRIPTION("The animated entity.") EntityId target,
        SENCHA_ARG("intent") SENCHA_LABEL("Intent") SENCHA_DESCRIPTION("What to present, as its rig names it.")
            GameplayTagId intent,
        SENCHA_ARG("lifetime") SENCHA_LABEL("Lifetime") SENCHA_DESCRIPTION("Fixed for `ticks`; impulse for one tick.")
            AnimPresentLifetime lifetime = AnimPresentLifetime::Impulse,
        SENCHA_ARG("ticks") SENCHA_LABEL("Ticks") SENCHA_DESCRIPTION("How long a fixed one lasts.")
            std::uint32_t ticks = 0);

    [[nodiscard]] const World& AuthoredWorld() const { return *Entities; }

private:
    World* Entities;
    const FixedSimulationLoop* Clock;
};

#if !defined(SENCHA_CODEGEN)
#  include <anim/AnimRequestVerbs.sencha.h>
#endif
