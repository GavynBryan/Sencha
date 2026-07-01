#pragma once

#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>
#include <gameplay_tags/GameplayTagId.h>

#include <type_traits>
#include <vector>

class World;
struct FixedLogicContext;

//=============================================================================
// Locomotion modes: an open, mode-agnostic state machine
//
// A locomotion mode is a zero-size marker component (OnGround, InAir, and any a
// game adds) plus a scheduled system that queries With<Marker>. Exactly one marker
// is present per character. Modes are added purely additively in a game module:
// register the marker, register the mode here, register the system. The engine
// never enumerates the modes and is never edited.
//
// Transition is decoupled and priority-arbitrated: an eligibility system calls
// RequestLocomotionMode when its condition holds; ApplyLocomotionModes (the
// generic arbiter) applies the highest-priority request per character, swapping
// the marker (type-erased) and projecting the mode's gameplay tag.
//=============================================================================

// Per-entity mode request, highest priority this tick wins; the arbiter consumes
// it. Priority 0 means "no request".
struct LocomotionModeRequest
{
    ComponentTypeId Marker;
    int Priority = 0;
};

static_assert(std::is_trivially_copyable_v<LocomotionModeRequest>,
              "LocomotionModeRequest must be trivially copyable to live in ECS chunks");

SENCHA_DECLARE_COMPONENT_TYPE(LocomotionModeRequest, "sencha.locomotion_mode_request");

// One registered mode: its marker identity and the gameplay tag it projects while
// active (e.g. movement.grounded), which abilities gate on.
struct LocomotionModeEntry
{
    ComponentTypeId Marker;
    GameplayTagId ActiveTag;
};

// The registered mode set (World resource). The arbiter reads it to know which
// markers are modes and which tag each projects. Mode-agnostic: adding a mode
// appends an entry; it does not change the arbiter.
class LocomotionModeRegistry
{
public:
    void Register(ComponentTypeId marker, GameplayTagId activeTag);
    [[nodiscard]] const std::vector<LocomotionModeEntry>& Entries() const { return Modes; }
    [[nodiscard]] GameplayTagId TagFor(ComponentTypeId marker) const;

private:
    std::vector<LocomotionModeEntry> Modes;
};

// Register a mode by marker type. Idempotent (updates the tag if re-registered).
template <typename Marker>
void RegisterLocomotionMode(LocomotionModeRegistry& registry, GameplayTagId activeTag)
{
    registry.Register(ResolveComponentTypeId<Marker>(), activeTag);
}

// Request a mode for an entity this tick. No-op if the entity carries no
// LocomotionModeRequest or a higher-priority request already won.
void RequestLocomotionMode(World& world, EntityId entity, ComponentTypeId marker, int priority);

// The arbiter: apply each character's winning request by swapping its mode marker
// (type-erased, via the World's registered mode set) and its projected gameplay
// tag, then clear the request. Mode-agnostic; adding a mode never touches this.
void ApplyLocomotionModes(World& world);

// Schedule adapter: run the arbiter over each active logic registry per fixed tick.
class LocomotionModeArbiter
{
public:
    void FixedLogic(FixedLogicContext& ctx);
};
