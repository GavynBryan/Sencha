#pragma once

#include <attributes/AttributeSet.h>
#include <authored/AuthoredAnnotations.h>
#include <authored/AuthoredApiDefinition.h>
#include <authored/VerbInvocation.h>
#include <authored/VerbResult.h>
#include <core/metadata/EnumSchema.h>
#include <effects/EffectDefinitionData.h>
#include <gameplay_tags/GameplayTagQueryNames.h>
#include <math/Vec.h>
#include <time/FrameClock.h>

#include <array>
#include <cstdint>
#include <optional>

class FixedSimulationLoop;
class World;

enum class EffectRemoveRefusal : std::uint8_t
{
    None,
    NothingNamed,
    BothNamed,
};

template <>
struct EnumSchema<EffectRemoveRefusal>
{
    static constexpr std::array Values = {
        EnumValue{ EffectRemoveRefusal::None, "none" },
        EnumValue{ EffectRemoveRefusal::NothingNamed, "nothing_named", "Nothing named",
                   "Neither an effect nor kinds was given." },
        EnumValue{ EffectRemoveRefusal::BothNamed, "both_named", "Both named",
                   "An effect and kinds were both given; name one." },
    };
};

// The authored API's door to effects. Applying and holding refuse a target without an
// attribute set, or without an attribute the effect modifies.
class EffectOperations
{
public:
    EffectOperations(World& world, const FixedSimulationLoop& clock)
        : Entities(&world)
        , Clock(&clock)
    {
    }

    SENCHA_VERB("effect.apply")
    SENCHA_LABEL("Apply effect")
    SENCHA_CATEGORY("Effects")
    SENCHA_DESCRIPTION("Applies an effect to an entity's attributes.")
    VerbAnswer Apply(const VerbInvocation& invocation,
                     SENCHA_TARGET("target", AttributeSet) SENCHA_LABEL("Target")
                         SENCHA_DESCRIPTION("The entity whose attributes the effect modifies.") EntityId target,
                     SENCHA_ARG("effect") SENCHA_LABEL("Effect") SENCHA_DATA_ASSET(kEffectDefinitionType)
                         AuthoredDataRef effect,
                     SENCHA_ARG("scale") SENCHA_LABEL("Scale") SENCHA_DESCRIPTION("Multiplies every modifier's magnitude.")
                         float scale = 1.0f,
                     SENCHA_ARG("instigator") SENCHA_LABEL("Instigator")
                         SENCHA_DESCRIPTION("Who applied it, for whatever reacts to it.")
                             std::optional<EntityId> instigator = std::nullopt,
                     SENCHA_ARG("from") SENCHA_LABEL("From")
                         SENCHA_DESCRIPTION("Where it came from, for whatever reacts to it. The instigator when absent.")
                             std::optional<Vec3d> from = std::nullopt,
                     SENCHA_ARG("keep") SENCHA_LABEL("Keep")
                         SENCHA_DESCRIPTION("Whether a save keeps it while it lasts. When absent, it is kept, as "
                                            "anything nothing owns is.") std::optional<bool> keep = std::nullopt);

    SENCHA_VERB("effect.remove")
    SENCHA_LABEL("Remove effects")
    SENCHA_CATEGORY("Effects")
    SENCHA_DESCRIPTION("Ends an entity's lasting effects of one definition, or of the kinds a tag query matches.")
    SENCHA_RESULT("count")
    VerbResult<std::int64_t, EffectRemoveRefusal> Remove(
        SENCHA_TARGET("target") SENCHA_LABEL("Target") SENCHA_DESCRIPTION("The entity whose lasting effects end.")
            EntityId target,
        SENCHA_ARG("effect") SENCHA_LABEL("Effect") SENCHA_DATA_ASSET(kEffectDefinitionType)
            SENCHA_DESCRIPTION("Ends every instance of this effect. Name it or kinds, not both.")
                std::optional<AuthoredDataRef> effect = std::nullopt,
        SENCHA_ARG("kinds") SENCHA_LABEL("Kinds")
            SENCHA_DESCRIPTION("Ends every effect whose kinds this query matches, each tag by name and beneath it. Name "
                               "it or an effect, not both.") std::optional<GameplayTagQueryNames> kinds = std::nullopt);

    SENCHA_LEASE("effect.hold")
    SENCHA_LABEL("Hold effect")
    SENCHA_CATEGORY("Effects")
    SENCHA_DESCRIPTION("Keeps a lasting effect on an entity until its holder lets go.")
    VerbAnswer Hold(const VerbInvocation& invocation,
                    SENCHA_TARGET("target", AttributeSet) SENCHA_LABEL("Target")
                        SENCHA_DESCRIPTION("The entity whose attributes the effect modifies.") EntityId target,
                    SENCHA_ARG("effect") SENCHA_LABEL("Effect") SENCHA_DATA_ASSET(kEffectDefinitionType)
                        AuthoredDataRef effect,
                    SENCHA_ARG("scale") SENCHA_LABEL("Scale") SENCHA_DESCRIPTION("Multiplies every modifier's magnitude.")
                        float scale = 1.0f,
                    SENCHA_ARG("instigator") SENCHA_LABEL("Instigator")
                        SENCHA_DESCRIPTION("Who applied it, for whatever reacts to it.")
                            std::optional<EntityId> instigator = std::nullopt,
                    SENCHA_ARG("from") SENCHA_LABEL("From")
                        SENCHA_DESCRIPTION("Where it came from, for whatever reacts to it. The instigator when absent.")
                            std::optional<Vec3d> from = std::nullopt);
    SENCHA_LEASE_RELEASE("effect.hold")
    VerbAdmission EndHold(const VerbInvocation& invocation);

    [[nodiscard]] const World& AuthoredWorld() const { return *Entities; }

private:
    [[nodiscard]] FixedSimTime Now(const VerbInvocation& invocation) const;

    World* Entities;
    const FixedSimulationLoop* Clock;
};

#if !defined(SENCHA_CODEGEN)
#  include <effects/EffectVerbs.sencha.h>
#endif
