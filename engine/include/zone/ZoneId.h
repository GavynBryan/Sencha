#pragma once

#include <core/identity/StrongId.h>

// The persistent zone identity: minted by the editor when a zone is created,
// serialized in the world partition manifest, and used verbatim as the runtime
// residency key in ZoneRuntime. Zero is invalid (StrongId convention).
using ZoneId = StrongId<struct ZoneIdTag, uint64_t>;
