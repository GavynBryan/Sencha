#pragma once

#include <anim/AnimRequestSet.h>
#include <ecs/ComponentAnnotations.h>

#include <cstddef>

struct AnimBoundRig;

// What this machine presents on an entity's presentation layers, apart from the
// requests the simulation makes: written by anim.present wherever something is
// presented, never replicated, journaled or predicted, and read by presentation
// layers alone (docs/gameplay/animation.md, "Presentation layers").
struct SENCHA_COMPONENT("sencha.anim_presentation_requests") AnimPresentationRequests
{
    AnimRequestSet Set;
};

// The requests `layer` reads: a presentation layer's own, every other layer the
// simulation's. Null when the entity carries no such set.
[[nodiscard]] const AnimRequestSet* AnimLayerRequests(const AnimBoundRig& rig, std::size_t layer,
                                                      const AnimRequestSet* simulation,
                                                      const AnimPresentationRequests* presentation);

#if !defined(SENCHA_CODEGEN)
#  include <anim/AnimPresentationRequests.sencha.h>
#endif
