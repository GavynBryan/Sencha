#pragma once

#include <ecs/ComponentAnnotations.h>
#include <ecs/EntityId.h>

#include <type_traits>

//=============================================================================
// AnimFactSubject
//
// On a rigged entity: the entity whose values the rig's `"from": "subject"`
// slots are gathered from. A held rifle reads its holder's stride and look; a
// worn cape reads its wearer's speed. Runtime-only: who holds what is a fact
// about the running process. With no subject, or a dead one, those slots keep
// the value they last had.
//=============================================================================
struct SENCHA_COMPONENT("sencha.anim_fact_subject") AnimFactSubject
{
    EntityId Subject;
};

static_assert(std::is_trivially_copyable_v<AnimFactSubject>,
              "AnimFactSubject must be trivially copyable to live in ECS chunks");

#if !defined(SENCHA_CODEGEN)
#  include <anim/AnimFactSubject.sencha.h>
#endif
