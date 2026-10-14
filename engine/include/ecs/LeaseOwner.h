#pragma once

#include <ecs/EntityId.h>

#include <cstdint>

// Whoever holds a lasting operation and ends it: an ability activation's op, a game
// system. Opaque to the domain holding the operation beyond identity and the
// entity's liveness, which bounds every hold.
struct LeaseOwner
{
    EntityId Entity;
    std::uint64_t Key = 0;

    [[nodiscard]] bool IsSet() const { return Entity.IsValid(); }
    friend bool operator==(const LeaseOwner&, const LeaseOwner&) = default;
};
