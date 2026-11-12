#pragma once

#include <attributes/AttributeId.h>
#include <ecs/EntityId.h>
#include <effects/EffectId.h>
#include <effects/EffectSource.h>

#include <cstdint>
#include <span>
#include <vector>

class AttributeSet;

// One attribute's Base as an application found it and as it left it, clamped.
struct EffectAttributeChange
{
    AttributeId Attribute;
    float Before = 0.0f;
    float After = 0.0f;
};

// One application that changed Base: an instant effect landing, or a periodic
// one's period falling due.
struct EffectApplication
{
    EntityId Target;
    EffectId Effect;
    EffectSource Source;
    float Scale = 1.0f;
    std::uint32_t FirstChange = 0;
    std::uint32_t ChangeCount = 0;
};

// The applications since the log was last cleared, as a World resource. Effects
// record into it only while a World holds one, and whoever adds it clears it
// once a tick after reading; docs/gameplay/abilitykit.md, "Effects".
class EffectApplicationLog
{
public:
    // Opens an application, then notes the Base of each attribute it is about
    // to change, then settles each to what the set holds once it has applied.
    void Begin(EntityId target, EffectId effect, const EffectSource& source, float scale);
    void Note(AttributeId attribute, float before);
    void Settle(const AttributeSet& set);
    void Clear();

    [[nodiscard]] std::span<const EffectApplication> Applications() const { return Rows; }
    [[nodiscard]] std::span<const EffectAttributeChange> ChangesOf(const EffectApplication& application) const;
    // The change `application` made to `attribute`: After minus Before, zero when it made none.
    [[nodiscard]] float ChangeIn(const EffectApplication& application, AttributeId attribute) const;

private:
    std::vector<EffectApplication> Rows;
    std::vector<EffectAttributeChange> Changes;
};
