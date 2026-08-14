#pragma once

#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>

#include <cstdint>
#include <string_view>

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
struct TurretMount
{
    // Where it is pointing. Replicated to everybody, because a turret turning
    // is exactly what another player sees of somebody driving it.
    float Yaw = 0.0f;
    // Rounds left. Only whoever is driving it has any use for the number, and
    // being a narrow integer beside a wider field it also keeps the codec
    // honest about owner gating.
    std::uint8_t Rounds = 12;
};

template <>
struct TypeSchema<TurretMount>
{
    static constexpr std::string_view Name = "turret_mount";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('T', 'U', 'R', 'T');
    static constexpr bool Replicated = true;

    static auto Fields()
    {
        return std::tuple{
            // A tenth of a degree is finer than anyone can see a gun move, and
            // it is a third of the bits a full float would spend saying so.
            MakeField("yaw", &TurretMount::Yaw).Quantize(-3.2f, 3.2f, 12),
            MakeField("rounds", &TurretMount::Rounds).OwnerOnly(),
        };
    }
};

// Where a driver's body waits while they are in the turret is not recorded
// here. It is NetPlayerControl::Body on the player, which is where it belongs:
// the body is a fact about the person, not about the gun, and every other
// mechanism that takes somebody out of their body would otherwise need its own
// copy of it.
