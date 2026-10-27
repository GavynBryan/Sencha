#pragma once

#include <core/text/InlineString.h>
#include <ecs/ComponentAnnotations.h>

#include <cstdint>
#include <type_traits>

//=============================================================================
// Projectile
//
// Something launched that flies on its own and does something to what it meets:
// a bolt, an arrow, a fireball, a mortar round, a thrown rock. Authored on the
// root of a prefab, which is everything else it is -- its mesh, its light, its
// sound -- so a launch spawns the prefab and this says how it moves and what a
// contact does. Nothing here knows what launched it or what kind of game it is.
//
// The simulation authority flies it and decides every contact. Other machines
// receive the prefab and draw it where the authority said it was
// (docs/gameplay/projectiles.md).
//
// Names rather than ids for its effects and tags, resolved once per prefab the
// first time one is launched: a prefab is content, and a content reference that
// does not resolve is reported by name and the launch refused. Each name is
// optional, and an empty or absent one means none.
//=============================================================================
struct SENCHA_COMPONENT("sencha.projectile")
       SENCHA_SCHEMA("Projectile")
       SENCHA_SCENE_CHUNK("PROJ")
Projectile
{
    SENCHA_FIELD("speed")
    SENCHA_TOOLTIP("Metres per second it leaves at.")
    float Speed = 40.0f;

    SENCHA_FIELD("gravity_scale")
    SENCHA_LABEL("Gravity")
    SENCHA_TOOLTIP("How much of the world's gravity pulls it down: zero flies straight, one falls as a "
                   "dropped stone does.")
    float GravityScale = 0.0f;

    SENCHA_FIELD("radius")
    SENCHA_TOOLTIP("The sphere it sweeps along its path, in metres.")
    float Radius = 0.1f;

    SENCHA_FIELD("lifetime")
    SENCHA_TOOLTIP("Seconds it flies before it ends on its own.")
    float Lifetime = 3.0f;

    SENCHA_FIELD("max_hits")
    SENCHA_LABEL("Most hits")
    SENCHA_TOOLTIP("How many entities it may strike before it stops, at most 16. Only entities carrying the "
                   "pass-through tag let it go on; anything else stops it whatever this is.")
    std::uint32_t MaxHits = 1;

    SENCHA_FIELD("passes_through")
    SENCHA_OPTIONAL
    SENCHA_LABEL("Passes through")
    SENCHA_TOOLTIP("A gameplay tag: an entity carrying it is struck and passed through, while hits remain. "
                   "Empty passes through nothing.")
    InlineString<64> PassesThrough;

    SENCHA_FIELD("hit_effect")
    SENCHA_OPTIONAL
    SENCHA_LABEL("Hit effect")
    SENCHA_TOOLTIP("An effect applied to each entity it strikes that has attributes, as "
                   "asset://effects/<name>.sdata. Empty applies none.")
    InlineString<192> HitEffect;

    SENCHA_FIELD("scaled_effect")
    SENCHA_OPTIONAL
    SENCHA_LABEL("Scaled effect")
    SENCHA_TOOLTIP("An effect applied beside the hit effect, scaled by the launch's magnitude -- a charge, a "
                   "distance, whatever the launcher measured. Skipped when the magnitude is zero.")
    InlineString<192> ScaledEffect;

    SENCHA_FIELD("splash_radius")
    SENCHA_LABEL("Splash radius")
    SENCHA_TOOLTIP("Metres around where it ends that its effects also reach, scaled by the launch's splash "
                   "scale. Zero has no splash.")
    float SplashRadius = 0.0f;

    SENCHA_FIELD("splash_effect")
    SENCHA_OPTIONAL
    SENCHA_LABEL("Splash effect")
    SENCHA_TOOLTIP("The effect applied within the splash. Empty applies the hit effects instead.")
    InlineString<192> SplashEffect;

    SENCHA_FIELD("splash_needs_sight")
    SENCHA_LABEL("Splash needs sight")
    SENCHA_TOOLTIP("Splash reaches only what has a clear line from where it ended.")
    bool SplashNeedsSight = true;

    SENCHA_FIELD("splash_on_expire")
    SENCHA_LABEL("Splash on expire")
    SENCHA_TOOLTIP("It splashes when its lifetime runs out, as a fused shell does, not only when it strikes "
                   "something.")
    bool SplashOnExpire = false;

    SENCHA_FIELD("hit_kind")
    SENCHA_OPTIONAL
    SENCHA_LABEL("Hit kind")
    SENCHA_TOOLTIP("A gameplay tag the projectile.hit occurrence carries, so a response can tell one kind "
                   "of shot from another.")
    InlineString<64> HitKind;

    SENCHA_FIELD("impact_cue")
    SENCHA_OPTIONAL
    SENCHA_LABEL("Impact cue")
    SENCHA_TOOLTIP("A cue appended to the instigator's cue log wherever it strikes or ends, for its "
                   "presentation. Empty appends none.")
    InlineString<64> ImpactCue;

    SENCHA_FIELD("hits_characters")
    SENCHA_LABEL("Hits characters")
    SENCHA_TOOLTIP("It strikes walking characters, whose capsules no physics query sees.")
    bool HitsCharacters = true;

    SENCHA_FIELD("ends_with_instigator")
    SENCHA_LABEL("Ends with instigator")
    SENCHA_TOOLTIP("It ends when whatever launched it is gone; otherwise it flies on.")
    bool EndsWithInstigator = false;
};

static_assert(std::is_trivially_copyable_v<Projectile>, "Projectile must be trivially copyable to live in ECS chunks");

#if !defined(SENCHA_CODEGEN)
#  include <projectile/Projectile.sencha.h>
#endif
