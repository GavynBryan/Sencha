#pragma once

#include <cstddef>

// Allocations the test binary has made through any form of operator new. The
// allocation functions can be replaced once per program, so a binary's
// allocation tests share this counter; it is safe to read from any thread.
[[nodiscard]] std::size_t AllocationCount();
