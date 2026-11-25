#pragma once

#include <authored/VerbInvocation.h>
#include <authored/VerbRegistry.h>
#include <core/metadata/EnumSchema.h>
#include <time/FrameClock.h>

#include <array>
#include <cstdint>
#include <string_view>

class FixedSimulationLoop;
class World;

// Apply an effect to an entity with attributes, unowned: an instant effect lands, a
// lasting one runs out on its own.
inline constexpr std::string_view kEffectApplyVerb = "effect.apply";
// Hold a lasting effect on an entity for as long as the lease's owner holds it.
inline constexpr std::string_view kEffectHoldLease = "effect.hold";
// End the lasting effects on an entity of one definition, or of kinds a tag query matches; answers how many.
inline constexpr std::string_view kEffectRemoveVerb = "effect.remove";

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

// The contracts, declared with the engine's own vocabulary.
void DeclareEffectVerbs(VerbRegistrationScope& scope);

// The authored API's door to effects. Both refuse a target without an attribute set,
// or without an attribute the effect modifies.
class EffectOperations
{
public:
    EffectOperations(World& world, const FixedSimulationLoop& clock)
        : Entities(&world)
        , Clock(&clock)
    {
    }

    VerbAdmission Apply(const VerbInvocation& invocation);
    VerbAdmission Hold(const VerbInvocation& invocation);
    VerbAnswer Remove(const VerbInvocation& invocation);

private:
    [[nodiscard]] FixedSimTime Now(const VerbInvocation& invocation) const;

    World* Entities;
    const FixedSimulationLoop* Clock;
};

inline VerbAdmission InvokeEffectApply(EffectOperations& operations, const VerbInvocation& invocation)
{
    return operations.Apply(invocation);
}

inline VerbAdmission InvokeEffectHold(EffectOperations& operations, const VerbInvocation& invocation)
{
    return operations.Hold(invocation);
}

inline VerbAnswer InvokeEffectRemove(EffectOperations& operations, const VerbInvocation& invocation)
{
    return operations.Remove(invocation);
}
