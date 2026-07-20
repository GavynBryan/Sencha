#pragma once

#include <cstdint>
#include <world/registry/RegistryId.h>
#include <zone/ZoneParticipation.h>

struct Registry;

//=============================================================================
// Registry residency transitions
//
// ZoneRuntime records one change per registry whose attachment or
// participation mutated during the drain window, coalesced to first-observed
// Previous and final Current (intermediate states were never observable to
// any frame). The batch is processed once per rendered frame in the
// RegistryResidency phase — after async commits apply lifecycle requests,
// before the frame view is built — so retained backend state (bodies, movers,
// constraints, voices) reacts to lifecycle before any frame span can observe
// the registry, and a Detaching registry gets its final visit while it and
// all of its resources are still alive. Destruction happens only after the
// batch is processed.
//=============================================================================

enum class RegistryResidencyChangeKind : uint8_t
{
    // Newly attached this window. Current carries the initial participation;
    // attach-then-SetParticipation in one window coalesces into this.
    Attached,

    ParticipationChanged,

    // DestroyZone was requested. Instance stays valid (readable, resources
    // alive) until the batch has been processed; the registry is destroyed
    // immediately afterward.
    Detaching,
};

struct RegistryResidencyChange
{
    RegistryResidencyChangeKind Kind = RegistryResidencyChangeKind::ParticipationChanged;

    RegistryId Id;
    Registry* Instance = nullptr; // valid while the batch is processed

    ZoneParticipation Previous;
    ZoneParticipation Current;
};
