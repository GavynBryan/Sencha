#pragma once

#include <ecs/EntityId.h>
#include <gameplay_tags/GameplayTagId.h>
#include <navigation/NavigationTypes.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

class GameplayTagRegistry;
class World;
class ZoneNavigation;
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
    // The ability the agent's policy crosses this kind with; empty for none.
    std::string_view Ability = {};
};

// How one traversal kind is crossed: a registered operation, chosen by the
// link's kind tag. The follower calls Step each tick until it stops running,
// and Abort, with the executor's memory, when the order is replaced or cancelled
// mid-crossing.
struct NavTraversalExecutor
{
    NavTraversalResult (*Step)(NavTraversalFrame&) = nullptr;
    void (*Abort)(World&, EntityId agent, const NavRouteStep&, std::uint32_t memory) = nullptr;
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
// Starts the ability the agent's policy names for the kind, and is over once it
// has ended and the agent stands: succeeded at the exit, failed anywhere else.
NavTraversalResult NavTraverseByAbility(NavTraversalFrame& frame);
void NavAbortAbilityTraversal(World& world, EntityId agent, const NavRouteStep& step, std::uint32_t memory);

inline constexpr NavTraversalExecutor kNavWalkAcross{ &NavTraverseByWalking, nullptr };
inline constexpr NavTraversalExecutor kNavJumpAcross{ &NavTraverseByJumping, nullptr };
inline constexpr NavTraversalExecutor kNavCrossByAbility{ &NavTraverseByAbility, &NavAbortAbilityTraversal };

// The traversal kinds `agent` may plan through in `navigation`: those its tags
// grant and an executor can carry out. Returns how many were written to `out`.
std::size_t CollectNavCapabilities(const World& world, EntityId agent, const ZoneNavigation& navigation,
                                   std::span<GameplayTagId> out);

// Where a rope from `entry` to `anchor` comes closest to above `exit`: the point
// to let go and drop onto the exit, for a character `height` tall, in the
// character's centre. Nullopt when the rope passes too far from above the exit.
[[nodiscard]] std::optional<Vec3d> NavAnchoredRelease(const Vec3d& entry, const Vec3d& exit, const Vec3d& anchor,
                                                      float height);

// One agent crossing by ability, for the ability to ask where it is going. Every
// point is where the character's centre belongs but the anchor, which is fixed.
struct NavAbilityCrossing
{
    EntityId Agent;
    Vec3d Start = Vec3d::Zero();
    Vec3d Exit = Vec3d::Zero();
    Vec3d Anchor = Vec3d::Zero();
    Vec3d Release = Vec3d::Zero();
    bool Anchored = false;
};

struct NavAbilityCrossings
{
    std::vector<NavAbilityCrossing> Active;
};
