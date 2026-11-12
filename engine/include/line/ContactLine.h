#pragma once

#include <core/text/InlineString.h>
#include <ecs/ComponentAnnotations.h>
#include <ecs/EntityId.h>
#include <ecs/LeaseOwner.h>
#include <effects/EffectId.h>
#include <gameplay_tags/GameplayTagId.h>
#include <math/Vec.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

// Characters one line may strike while it is out.
inline constexpr std::size_t kContactLineMaxHits = 8;

//=============================================================================
// ContactLine
//
// A straight line from an entity's aim origin out to a tip, thrown by a
// projectile: it follows the projectile out, stays where the projectile met
// level geometry, and strikes characters that touch it. Authored on the root of
// a prefab whose meshes draw it; the root stands at the tip. The projectile only
// finds what the tip meets. See docs/gameplay/contact-lines.md.
//=============================================================================
struct SENCHA_COMPONENT("sencha.contact_line")
       SENCHA_SCHEMA("Contact Line")
       SENCHA_SCENE_CHUNK("CLIN")
ContactLine
{
    SENCHA_FIELD("radius")
    SENCHA_TOOLTIP("Metres around the line that strike a character.")
    float Radius = 0.12f;

    SENCHA_FIELD("effect")
    SENCHA_OPTIONAL
    SENCHA_LABEL("Effect")
    SENCHA_TOOLTIP("An effect applied to a character the line strikes, as asset://effects/<name>.sdata.")
    InlineString<192> Effect;

    SENCHA_FIELD("kind")
    SENCHA_OPTIONAL
    SENCHA_LABEL("Kind")
    SENCHA_TOOLTIP("The gameplay tag the projectile.hit occurrence of a strike carries.")
    InlineString<64> Kind;

    SENCHA_FIELD("ignores")
    SENCHA_OPTIONAL
    SENCHA_LABEL("Ignores")
    SENCHA_TOOLTIP("A gameplay tag: a character carrying it is not struck.")
    InlineString<64> Ignores;

    SENCHA_FIELD("anchor_seconds")
    SENCHA_LABEL("Anchor seconds")
    SENCHA_TOOLTIP("How long the tip stays where it stopped before it comes away on its own.")
    float AnchorSeconds = 5.0f;

    SENCHA_FIELD("retract_speed")
    SENCHA_LABEL("Retract speed")
    SENCHA_TOOLTIP("Metres per second the tip returns at once the line is let go or stops short. It strikes "
                   "nothing on the way. Zero ends the line at once instead.")
    float RetractSpeed = 40.0f;

    SENCHA_FIELD("max_length")
    SENCHA_OPTIONAL
    SENCHA_LABEL("Max length")
    SENCHA_TOOLTIP("Metres the throw may fly before it comes home. Zero leaves it to the projectile's lifetime.")
    float MaxLength = 0.0f;

    SENCHA_FIELD("leash_slack")
    SENCHA_OPTIONAL
    SENCHA_LABEL("Leash slack")
    SENCHA_TOOLTIP("Metres a character may stray past where it stood when a kept line anchored. Zero leaves it "
                   "free to walk away.")
    float LeashSlack = 0.0f;

    SENCHA_FIELD("rehit_seconds")
    SENCHA_OPTIONAL
    SENCHA_LABEL("Rehit seconds")
    SENCHA_TOOLTIP("Seconds a struck character must stay off the line before it can be struck again. Zero strikes "
                   "each once.")
    float RehitSeconds = 0.0f;

    SENCHA_FIELD("slots")
    SENCHA_OPTIONAL
    SENCHA_LABEL("Slots")
    SENCHA_TOOLTIP("How many lines an instigator can have out at once, a hand each. A throw with every slot taken "
                   "draws its oldest kept line home. Zero sets no limit and throws from the aim origin.")
    std::uint32_t Slots = 0;

    SENCHA_FIELD("slot_spacing")
    SENCHA_OPTIONAL
    SENCHA_LABEL("Slot spacing")
    SENCHA_TOOLTIP("Metres between slots across the instigator's aim.")
    float SlotSpacing = 0.0f;

    SENCHA_FIELD("anchor_cue")
    SENCHA_OPTIONAL
    SENCHA_LABEL("Anchor cue")
    SENCHA_TOOLTIP("A cue appended to the instigator's cue log where the tip stops in level geometry.")
    InlineString<64> AnchorCue;

    SENCHA_FIELD("cut_cue")
    SENCHA_OPTIONAL
    SENCHA_LABEL("Cut cue")
    SENCHA_TOOLTIP("A cue appended to the instigator's cue log where the line is cut.")
    InlineString<64> CutCue;
};

// A mesh of a line's prefab stretched from the tip back to the instigator's aim
// origin each frame. Sits under the prefab's root; +Z runs along the line.
struct SENCHA_COMPONENT("sencha.contact_line_span")
       SENCHA_SCHEMA("Contact Line Span")
       SENCHA_SCENE_CHUNK("CLSP")
ContactLineSpan
{
    SENCHA_FIELD("mesh_length")
    SENCHA_LABEL("Mesh length")
    SENCHA_TOOLTIP("Metres the mesh spans along its +Z, centred on its origin.")
    float MeshLength = 1.0f;
};

enum class ContactLineStage : std::uint8_t
{
    Extending,
    Anchored,
    Retracting,
};

// The authority's working state for a line that is out.
struct SENCHA_COMPONENT("sencha.contact_line_state") ContactLineState
{
    LeaseOwner Owner;
    EntityId Instigator;
    EntityId Projectile;
    Vec3d Tip = Vec3d::Zero();
    ContactLineStage Stage = ContactLineStage::Extending;
    // Seconds left of its current stage.
    float Remaining = 0.0f;
    // How far the instigator stood from the tip when it anchored.
    float Length = 0.0f;
    // Metres right of the aim origin it runs from, for the slot it holds.
    float Side = 0.0f;
    std::array<EntityId, kContactLineMaxHits> Hits{};
    // Seconds each struck character has been off the line.
    std::array<float, kContactLineMaxHits> OffSeconds{};
    std::uint8_t HitCount = 0;
    // Ticks in a row something solid has stood across it.
    std::uint8_t BlockedTicks = 0;
    std::uint8_t Slot = 0;
    // Stays anchored when let go, until it times out or is drawn home.
    bool Kept = false;
    EffectId Effect;
    GameplayTagId Kind;
    GameplayTagId Ignores;
    GameplayTagId AnchorCue;
    GameplayTagId CutCue;
};

static_assert(std::is_trivially_copyable_v<ContactLine>);
static_assert(std::is_trivially_copyable_v<ContactLineSpan>);
static_assert(std::is_trivially_copyable_v<ContactLineState>);

#if !defined(SENCHA_CODEGEN)
#  include <line/ContactLine.sencha.h>
#endif
