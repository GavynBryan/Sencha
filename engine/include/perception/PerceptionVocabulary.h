#pragma once

#include <authored/AuthoredQueryDispatcher.h>
#include <authored/VerbDispatcher.h>
#include <gameplay_tags/GameplayTagId.h>

#include <string_view>
#include <vector>

class World;

inline constexpr std::string_view kSightSenseTag = "perception.sense.sight";
inline constexpr std::string_view kHearingSenseTag = "perception.sense.hearing";

// Stages a stimulus for hearing: {source?, position?, radius, only?}. Without a
// position it happens where the source stands; with `only` it reaches that
// observer alone. Authority only.
inline constexpr std::string_view kPerceptionStimulusVerb = "perception.stimulus";

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
    GameplayTagId Hearing;
};

// The sense tags, a GameplayTagRegistry if the World has none, as a World
// resource. Idempotent.
const PerceptionSenses& EnsurePerceptionSenses(World& world);

// Senses, an empty PerceptionFrame, and the perception queries declared into
// the World's authored catalog. Runtime and editors both install it, so content
// validates without a game running. False when a declaration was refused.
[[nodiscard]] bool InstallPerceptionVocabulary(World& world);

// perception.stimulus, staging into `world`, for as long as the token lives.
[[nodiscard]] VerbBindingToken BindPerceptionVerbs(VerbDispatcher& dispatcher, World& world);

// Answers the perception queries from `world`'s PerceptionFrame for as long as
// the tokens live. A World nobody scans answers Unavailable.
[[nodiscard]] std::vector<AuthoredQueryBindingToken> BindPerceptionQueries(AuthoredQueryDispatcher& dispatcher,
                                                                          const World& world);
