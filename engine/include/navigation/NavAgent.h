#pragma once

#include <assets/data/DataAssetHandle.h>
#include <core/identity/StrongId.h>
#include <core/text/InlineString.h>
#include <ecs/ComponentAnnotations.h>
#include <ecs/ComponentTraits.h>
#include <ecs/EntityId.h>
#include <math/Vec.h>
#include <navigation/NavigationPolicyData.h>
#include <navigation/NavigationTypes.h>
#include <world/ComponentAssetOwnership.h>

#include <cstdint>
#include <tuple>
#include <type_traits>

// One navigation order given to one agent. Minted per agent by
// RequestNavigation and CancelNavigation; zero means none was ever given.
using NavRequestId = StrongId<struct NavRequestIdTag, std::uint32_t>;

// The gameplay-tag name of a navigation build profile, stored inline so the
// component stays trivially copyable.
using NavProfileName = InlineString<64>;

// An entity that can be sent somewhere. Authored on whatever should walk by
// itself; its request and progress come with it. See
// docs/navigation/runtime.md, "Agents".
struct SENCHA_COMPONENT("sencha.navigation.agent")
       SENCHA_SCHEMA("Nav Agent")
       SENCHA_SCENE_CHUNK("NAGT")
NavAgent
{
    SENCHA_FIELD("profile")
    SENCHA_LABEL("Build profile")
    SENCHA_TOOLTIP("The navigation build profile this agent walks, for example "
                   "navigation.profile.humanoid. Its radius should cover the "
                   "agent's capsule.")
    NavProfileName Profile;

    SENCHA_FIELD("policy")
    SENCHA_DATA_ASSET(kNavigationPolicySubtype)
    SENCHA_LABEL("Policy")
    SENCHA_TOOLTIP("How this agent weighs ground and traversals. None walks "
                   "every area at its default cost.")
    DataAssetHandle Policy{};
};

enum class NavDestinationKind : std::uint8_t
{
    None,
    Point,
    Entity,
};

// The agent's current order. Written only through RequestNavigation and
// CancelNavigation; the follower reads it.
struct SENCHA_COMPONENT("sencha.navigation.agent_request") NavAgentRequest
{
    NavRequestId Id;
    NavDestinationKind Kind = NavDestinationKind::None;
    // Entity only: keep tracking the target after arriving instead of ending.
    bool Follow = false;
    Vec3d Point = Vec3d::Zero();
    EntityId Target;
    // Metres; zero means the nav.agent.arrive_radius default.
    float ArriveRadius = 0.0f;
    // In (0, 1]: the fraction of the agent's top speed it walks at. Anything
    // else is full speed.
    float SpeedScale = 1.0f;
};

enum class NavAgentPhase : std::uint8_t
{
    Idle,
    Planning,
    Moving,
    Traversing,
    Holding,
    Arrived,
    Failed,
    Cancelled,
};

enum class NavAgentFailure : std::uint8_t
{
    None,
    NoZone,
    ZoneUnavailable,
    ProfileUnavailable,
    StartOffNavigation,
    DestinationOffNavigation,
    DestinationInOtherZone,
    NoPath,
    SearchLimitReached,
    RouteTooLong,
    TargetLost,
    Stuck,
    TraversalFailed,
    TraversalUnsupported,
    TakenOverByInput,
};

enum class NavRepathReason : std::uint8_t
{
    None,
    ZoneReloaded,
    TileChanged,
    LinkChanged,
    ZoneChanged,
    TargetMoved,
    Deviated,
    Stuck,
    TraversalFailed,
};

// An index into the follower's route slots, honoured only while the slot's
// owner is this entity.
inline constexpr std::uint32_t kNavNoRouteSlot = 0xffffffffu;

// What became of the agent's current request. Written only by the follower.
struct SENCHA_COMPONENT("sencha.navigation.agent_progress") NavAgentProgress
{
    // The request every other field describes.
    NavRequestId Request;
    NavAgentPhase Phase = NavAgentPhase::Idle;
    // Why the request ended, when Phase is Failed.
    NavAgentFailure Failure = NavAgentFailure::None;
    NavRepathReason LastRepath = NavRepathReason::None;
    // The latest search's own status, kept apart from Failure.
    NavStatus LastQuery = NavStatus::Success;
    std::uint16_t Repaths = 0;
    // Metres along the route still to go.
    float Remaining = 0.0f;
    // Where the request was last planned to.
    Vec3d Destination = Vec3d::Zero();
    std::uint32_t RouteSlot = kNavNoRouteSlot;
};

static_assert(std::is_trivially_copyable_v<NavAgent>);
static_assert(std::is_trivially_copyable_v<NavAgentRequest>);
static_assert(std::is_trivially_copyable_v<NavAgentProgress>);

#if !defined(SENCHA_CODEGEN)
#  include <navigation/NavAgent.sencha.h>
#endif

// Owns one reference to its policy, and brings the request and progress every
// agent has.
template <>
struct ComponentTraits<NavAgent> : SchemaAssetOwnership<NavAgent>
{
    using DerivedComponents = std::tuple<NavAgentRequest, NavAgentProgress>;
};
