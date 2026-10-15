#pragma once

#include <ecs/EntityId.h>
#include <gameplay_tags/GameplayTagId.h>
#include <navigation/NavigationTypes.h>

#include <cstdint>
#include <unordered_map>

class GameplayTagRegistry;
class World;
struct MovementIntent;

enum class NavTraversalResult : std::uint8_t
{
    Running,
    Succeeded,
    Failed,
};

// One tick of one agent crossing one link.
struct NavTraversalFrame
{
    World& Entities;
    EntityId Agent;
    const NavRouteStep& Step;
    // Ticks since the crossing began; 0 on the first call.
    std::uint32_t Ticks = 0;
    // The agent's own seconds this tick.
    float AgentSeconds = 0.0f;
    // The build profile's height, the vertical tolerance for being somewhere.
    float Height = 1.8f;
    // The agent's intent this tick; an executor steers through this alone.
    MovementIntent& Intent;
    // Zero when the crossing begins; the executor's own between ticks.
    std::uint32_t& Memory;
};

// How one traversal kind is crossed: a registered operation, chosen by the
// link's kind tag. The follower calls Step each tick until it stops running,
// and Abort when the order is replaced or cancelled mid-crossing.
struct NavTraversalExecutor
{
    NavTraversalResult (*Step)(NavTraversalFrame&) = nullptr;
    void (*Abort)(World&, EntityId agent, const NavRouteStep&) = nullptr;
};

// The World's executors by traversal kind. A link's kind resolves to the
// executor registered for it or its nearest registered ancestor.
class NavTraversalExecutors
{
public:
    bool Register(GameplayTagId kind, NavTraversalExecutor executor);
    [[nodiscard]] const NavTraversalExecutor* Resolve(const GameplayTagRegistry& tags, GameplayTagId kind) const;
    [[nodiscard]] std::size_t Count() const { return Executors.size(); }

private:
    std::unordered_map<GameplayTagId, NavTraversalExecutor> Executors;
};

// Registers `executor` for `kind` in the world's table, creating it if needed.
bool RegisterNavTraversal(World& world, GameplayTagId kind, NavTraversalExecutor executor);

// Walks from entry to exit: drops, and gaps a step crosses.
NavTraversalResult NavTraverseByWalking(NavTraversalFrame& frame);
// Jumps at the entry and lands at the exit. Fails if the agent cannot leave the
// ground, or lands somewhere else.
NavTraversalResult NavTraverseByJumping(NavTraversalFrame& frame);

inline constexpr NavTraversalExecutor kNavWalkAcross{ &NavTraverseByWalking, nullptr };
inline constexpr NavTraversalExecutor kNavJumpAcross{ &NavTraverseByJumping, nullptr };
