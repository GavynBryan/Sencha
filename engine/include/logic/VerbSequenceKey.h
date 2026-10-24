#pragma once

#include <core/identity/StrongId.h>

#include <cstdint>

// A sequence named by the hash of its authored text. A value, not a lease: a
// component holding one can be copied, snapshotted and restored byte for byte.
using VerbSequenceKey = StrongId<struct VerbSequenceKeyTag, std::uint64_t>;
