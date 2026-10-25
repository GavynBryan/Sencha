#pragma once

#include <core/metadata/TypeSchema.h>
#include <net/ReplicationLayout.h>
#include <projectile/ProjectileState.h>

#include <cstdint>

// A flight as it travels: the instigator as a wire entity, the ticks as they are.
struct ProjectileFlightWire
{
    std::uint64_t Instigator = 0;
    std::uint64_t LaunchTick = 0;
    std::uint64_t EndTick = 0;
};

template <>
struct TypeSchema<ProjectileFlightWire>
{
    static constexpr std::string_view Name = "projectile_flight_wire";
    static auto Fields()
    {
        return std::tuple{ MakeField("instigator", &ProjectileFlightWire::Instigator),
                           MakeField("launch_tick", &ProjectileFlightWire::LaunchTick),
                           MakeField("end_tick", &ProjectileFlightWire::EndTick) };
    }
};

template <>
struct ReplicationCodec<ProjectileFlight>
{
    using Wire = ProjectileFlightWire;
    static void ToWire(const ReplicationWireContext& context, const ProjectileFlight& flight,
                       ProjectileFlightWire& wire);
    // Keeps this machine's Shown: whether it is drawing the projectile is its own.
    static void FromWire(const ReplicationWireContext& context, const ProjectileFlightWire& wire,
                         ProjectileFlight& flight);
};
