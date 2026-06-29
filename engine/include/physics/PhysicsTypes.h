#pragma once

#include <cstddef>
#include <cstdint>

//=============================================================================
// Physics public vocabulary
//
// Engine-typed, backend-free. Nothing here names or includes Jolt; the Jolt
// instance lives behind PhysicsWorld's PIMPL (see src/physics/PhysicsWorldImpl.h).
// Gameplay and ECS code depend only on these types.
//=============================================================================

// How a body participates in the simulation.
//   Static    - never moves; world geometry. No mass, no integration.
//   Kinematic - moved by setting its transform; pushes dynamics, ignores them.
//   Dynamic   - integrated from forces, gravity, and contacts.
enum class BodyMotion : uint8_t
{
    Static = 0,
    Kinematic = 1,
    Dynamic = 2,
};

// Coarse collision category. Drives broadphase pairing: Static never pairs with
// Static, everything solid pairs with Static and Moving, Trigger reports overlap
// without resolving it. A fixed, mechanical set; gameplay collision rules layer
// on top of contacts, not by inventing new layers here.
enum class CollisionLayer : uint8_t
{
    Static = 0,
    Moving = 1,
    Character = 2,
    Trigger = 3,
};

// Opaque handle to a body inside a PhysicsWorld. Mirrors the backend's body id
// width without exposing the backend type. Its invalid sentinel matches the
// backend's, so a default-constructed id is invalid (unlike StrongId, the all-
// ones value is the sentinel here because the backend treats index 0 as valid).
struct PhysicsBodyId
{
    static constexpr uint32_t kInvalid = 0xffffffffu;
    uint32_t Value = kInvalid;

    [[nodiscard]] bool IsValid() const { return Value != kInvalid; }
    friend bool operator==(PhysicsBodyId, PhysicsBodyId) = default;
};

template <>
struct std::hash<PhysicsBodyId>
{
    std::size_t operator()(PhysicsBodyId id) const noexcept
    {
        return std::hash<uint32_t>{}(id.Value);
    }
};
