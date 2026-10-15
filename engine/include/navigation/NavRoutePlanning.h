#pragma once

#include <navigation/NavQueryContext.h>
#include <navigation/NavigationTypes.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

class JobSystem;
class ZoneNavigation;

inline constexpr std::size_t kNavPlanAvoidLinks = 4;

// One route search, gathered on the owner thread. Plain values and pointers to
// state that does not change during FixedLogic; nothing points into the input
// itself, so it may be copied or moved freely.
struct NavPlanInput
{
    const ZoneNavigation* Zone = nullptr;
    GameplayTagId Profile;
    const NavQueryPolicy* Policy = nullptr;
    Vec3d From = Vec3d::Zero();
    Vec3d To = Vec3d::Zero();
    Vec3d Extents = Vec3d::Zero();
    // A zone binds at most kNavMaxTraversalKinds kinds, so every usable one fits.
    std::array<GameplayTagId, kNavMaxTraversalKinds> Capabilities{};
    std::uint8_t CapabilityCount = 0;
    std::array<NavLinkId, kNavPlanAvoidLinks> AvoidLinks{};
    std::uint8_t AvoidLinkCount = 0;
};

struct NavPlanOutput
{
    NavStatus Status = NavStatus::NoPath;
    NavLocation Start;
    NavLocation End;
    NavQueryDiagnostics Diagnostics;
};

// Projects both ends and searches between them into `route`. Reads only the
// input and the zone it names; writes only the context, route and result.
[[nodiscard]] NavPlanOutput PlanRoute(const NavPlanInput& input, NavQueryContext& context, NavRouteBuffer& route);

struct NavRouteCapacity
{
    std::size_t Steps = 32;
    std::size_t Corners = 64;
    std::size_t Dependencies = 128;
};

// Plans a batch as one fork-join: an output and route per input, a context per
// worker. Below `minParallel` inputs, or with no workers, the same PlanRoute runs
// inline in order. See docs/navigation/runtime.md, "Agents".
class NavRoutePlanner
{
public:
    NavRoutePlanner(JobSystem* jobs, NavQueryContextConfig context, NavRouteCapacity capacity);
    ~NavRoutePlanner();
    NavRoutePlanner(const NavRoutePlanner&) = delete;
    NavRoutePlanner& operator=(const NavRoutePlanner&) = delete;

    void Plan(std::span<const NavPlanInput> inputs, std::size_t minParallel);

    // Valid until the next Plan; index i belongs to input i.
    [[nodiscard]] const NavPlanOutput& Output(std::size_t i) const { return Outputs[i]; }
    [[nodiscard]] NavRouteBuffer& Route(std::size_t i) { return *Routes[i]; }

    [[nodiscard]] std::size_t ContextCount() const { return Contexts.size(); }

private:
    JobSystem* Jobs = nullptr;
    NavQueryContextConfig ContextConfig;
    NavRouteCapacity Capacity;
    std::vector<NavQueryContext> Contexts;
    std::vector<NavPlanOutput> Outputs;
    std::vector<std::unique_ptr<NavRouteBuffer>> Routes;
};
