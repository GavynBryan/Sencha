#pragma once

#include <cstddef>

// Every allocation the core test binary has made, counted by a replaced global
// operator new (AllocationCounter.cpp). Replacing it is allowed once per
// program, so every "a warmed call allocates nothing" test in this binary
// reads this one counter.
[[nodiscard]] std::size_t AllocationCount();
