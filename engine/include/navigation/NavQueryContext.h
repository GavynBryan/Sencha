#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

struct NavQueryContextConfig
{
    // Search nodes available to one query. A search that needs more stops with
    // SearchLimitReached rather than growing.
    std::uint32_t MaxNodes = 2048;
    // Longest polygon corridor a route may pass through.
    std::uint32_t MaxCorridor = 512;
};

//=============================================================================
// NavQueryContext
//
// Reusable scratch for navigation queries: the search node pool, open list, and
// corridor storage, sized once. One context serves one thread at a time; give
// each worker its own and queries run concurrently against the same zone
// without a lock. After the first query sizes it, a context allocates nothing.
//=============================================================================
class NavQueryContext
{
public:
    explicit NavQueryContext(NavQueryContextConfig config = {});
    ~NavQueryContext();
    NavQueryContext(NavQueryContext&&) noexcept;
    NavQueryContext& operator=(NavQueryContext&&) noexcept;
    NavQueryContext(const NavQueryContext&) = delete;
    NavQueryContext& operator=(const NavQueryContext&) = delete;

    [[nodiscard]] const NavQueryContextConfig& Config() const;

    // Backend state, complete only inside the navigation module.
    struct Backend;
    [[nodiscard]] Backend& GetBackend() { return *Impl; }

private:
    std::unique_ptr<Backend> Impl;
};
