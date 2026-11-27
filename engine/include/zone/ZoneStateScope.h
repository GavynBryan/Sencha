#pragma once

#include <core/identity/StrongId.h>
#include <zone/ZoneId.h>

#include <cstdint>

// A level as the zone store knows it, for the session: docs/plans/runtime-stable-identity.md.
using ZoneStateLevelId = StrongId<struct ZoneStateLevelIdTag, std::uint32_t>;

// One zone of one level, from ZoneStateStore::Scope; an invalid scope means no state applies.
struct ZoneStateScope
{
    ZoneStateLevelId Level;
    ZoneId Zone;

    [[nodiscard]] bool IsValid() const { return Level.IsValid() && Zone.IsValid(); }
    friend bool operator==(const ZoneStateScope&, const ZoneStateScope&) = default;
};
