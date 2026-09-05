#pragma once

#include <ecs/ComponentAnnotations.h>

#include <cstdint>

// A fixed gun somebody can take over, and the template's worked example of a
// networked object that is not a player pawn.
//
// It exists to be driven by whoever asks for it. Taking it goes the whole way
// through the engine's seams and nowhere else: a client names it by its network
// identity, sends a validated request, the authority checks the request against
// its own records and moves ownership, and everything that follows from
// ownership -- whose input steers it, what its driver can see of it, which
// entity this machine's camera answers to -- follows on its own. So a
// possession that breaks breaks here, in the game somebody actually runs,
// rather than only in a test.
struct SENCHA_COMPONENT("turret_mount")
       SENCHA_SCHEMA("turret_mount")
       SENCHA_SCENE_CHUNK("TURT")
       SENCHA_REPLICATED
TurretMount
{
    // Where it is pointing. Replicated to everybody, because a turret turning
    // is exactly what another player sees of somebody driving it. A tenth of a
    // degree is finer than anyone can see a gun move, and it is a third of the
    // bits a full float would spend saying so.
    SENCHA_FIELD("yaw")
    SENCHA_QUANTIZE(-3.2f, 3.2f, 12)
    SENCHA_DEGREES
    SENCHA_LABEL("Facing")
    SENCHA_TOOLTIP("Which way the gun starts out pointing.")
    float Yaw = 0.0f;

    // Rounds left. Only whoever is driving it has any use for the number, and
    // being a narrow integer beside a wider field it also keeps the codec
    // honest about owner gating.
    SENCHA_FIELD("rounds")
    SENCHA_OWNER_ONLY
    SENCHA_LABEL("Rounds loaded")
    std::uint8_t Rounds = 12;
};

// Where a driver's body waits while they are in the turret is not recorded
// here. It is ParticipantControl::Body on the participant, where it belongs:
// the body is a fact about the person, not about the gun, and every other
// mechanism that takes somebody out of their body would otherwise need its own
// copy of it.

#if !defined(SENCHA_CODEGEN)
#  include <TurretMount.sencha.h>
#endif
