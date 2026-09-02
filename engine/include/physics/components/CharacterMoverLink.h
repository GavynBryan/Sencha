#pragma once

#include <cstdint>

#include <ecs/ComponentAnnotations.h>

//=============================================================================
// CharacterMoverLink
//
// Runtime-only link from a CharacterController entity to its CharacterMover in
// the zone's CharacterMoverPool. CharacterMover owns a Jolt CharacterVirtual and
// is not trivially copyable, so it cannot live in a chunk; the mover lives in a
// system-side pool and the entity carries only its stable slot. The pool uses a
// free list so a slot stays valid for the mover's lifetime (no fixup on release).
// Written by the character bridge's reconcile; never authored or serialized.
//=============================================================================
struct SENCHA_COMPONENT("sencha.physics.mover_link") CharacterMoverLink
{
    uint32_t MoverSlot = 0;
};

#if !defined(SENCHA_CODEGEN)
#  include <physics/components/CharacterMoverLink.sencha.h>
#endif
