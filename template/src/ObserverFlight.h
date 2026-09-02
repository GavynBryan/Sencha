#pragma once

#include <ecs/ComponentAnnotations.h>

#include <type_traits>

//=============================================================================
// ObserverFlight
//
// Marks the body a game gets when its content did not load: it collides, and
// it goes where it is looking rather than where it is standing.
//
// Runtime-only and deliberately unauthored. This exists precisely when the
// authored pawn could not be built, so it may not depend on anything a
// designer wrote -- no profile, no prefab, no mesh. What it is made of is
// engine behaviour: a capsule, the free locomotion every character uses, and a
// forced vertical channel that takes the place gravity would have had.
//
// Two systems read it. The steering pass builds this body's wish direction
// from the full aim basis instead of flattening it onto the ground, and then
// forces the vertical channel from that wish -- which is what flying is, in
// the vocabulary the motion composition already has.
//=============================================================================
struct SENCHA_COMPONENT("template.observer_flight") ObserverFlight
{
};

static_assert(std::is_empty_v<ObserverFlight>,
              "ObserverFlight is a tag: it carries no data");

#if !defined(SENCHA_CODEGEN)
#  include <ObserverFlight.sencha.h>
#endif
