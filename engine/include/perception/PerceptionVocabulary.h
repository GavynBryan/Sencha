#pragma once

#include <authored/AuthoredQueryDispatcher.h>
#include <gameplay_tags/GameplayTagId.h>

#include <string_view>
#include <vector>

class World;

inline constexpr std::string_view kSightSenseTag = "perception.sense.sight";

// Sight only. True when seen this tick; false only when the observer's sight
// scan this tick was complete; otherwise Unavailable, never false.
inline constexpr std::string_view kSeesQuery = "perception.sees";
inline constexpr std::string_view kSeesTaggedQuery = "perception.sees_tagged";
// Unavailable when nothing matching was seen.
inline constexpr std::string_view kNearestSeenTaggedQuery = "perception.nearest_seen_tagged";

// From the observer's ObservationMemory. Unavailable off the authority or
// without a memory; a subject never observed is false for remembers and
// Unavailable for the rest.
inline constexpr std::string_view kRemembersQuery = "perception.remembers";
inline constexpr std::string_view kLastSeenPositionQuery = "perception.last_seen_position";
inline constexpr std::string_view kLastSeenVelocityQuery = "perception.last_seen_velocity";
inline constexpr std::string_view kSecondsSinceSeenQuery = "perception.seconds_since_seen";
inline constexpr std::string_view kSecondsTrackedQuery = "perception.seconds_tracked";
inline constexpr std::string_view kNearestRememberedTaggedQuery = "perception.nearest_remembered_tagged";

struct PerceptionSenses
{
    GameplayTagId Sight;
};

// The sense tags, a GameplayTagRegistry if the World has none, as a World
// resource. Idempotent.
const PerceptionSenses& EnsurePerceptionSenses(World& world);

// Senses, an empty PerceptionFrame, and the perception queries declared into
// the World's authored catalog. Runtime and editors both install it, so content
// validates without a game running. False when a declaration was refused.
[[nodiscard]] bool InstallPerceptionVocabulary(World& world);

// Answers the perception queries from `world`'s PerceptionFrame for as long as
// the tokens live. A World nobody scans answers Unavailable.
[[nodiscard]] std::vector<AuthoredQueryBindingToken> BindPerceptionQueries(AuthoredQueryDispatcher& dispatcher,
                                                                          const World& world);
