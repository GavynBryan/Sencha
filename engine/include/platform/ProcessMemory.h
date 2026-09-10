#pragma once

#include <cstdint>

// The bytes of physical memory the running process currently holds (its
// resident set), or 0 where the platform offers no such figure. A readout,
// not an accounting: it counts everything mapped and touched, including the
// driver's share, and it is read from the operating system on each call.
[[nodiscard]] std::uint64_t ProcessResidentBytes();
