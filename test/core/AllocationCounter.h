#pragma once

#include <cstddef>

// Allocations made by the core test binary. operator new can be replaced once
// per program, so every allocation test shares this counter.
[[nodiscard]] std::size_t AllocationCount();
