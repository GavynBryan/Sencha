#pragma once

#include <authored/AuthoredAnnotations.h>
#include <authored/AuthoredApiDefinition.h>
#include <authored/VerbInvocation.h>
#include <ecs/EntityId.h>
#include <gameplay_tags/GameplayTagId.h>
#include <math/Vec.h>

#include <optional>
#include <string_view>

class World;

inline constexpr std::string_view kSightSenseTag = "perception.sense.sight";
inline constexpr std::string_view kHearingSenseTag = "perception.sense.hearing";
// What an observer knows because another member of its group sensed it.
inline constexpr std::string_view kSharedSenseTag = "perception.sense.shared";

struct PerceptionSenses
{
    GameplayTagId Sight;
    GameplayTagId Hearing;
    GameplayTagId Shared;
};

// Whether `subject` is in `observer`'s standing sight scan: true between looks
// too, unlike a memory's age of zero.
[[nodiscard]] bool Sees(const World& world, EntityId observer, EntityId subject);

// The sense tags, a GameplayTagRegistry if the World has none, as a World
// resource. Idempotent.
const PerceptionSenses& EnsurePerceptionSenses(World& world);

// Senses, an empty PerceptionFrame, and the perception queries declared into
// the World's authored catalog. Runtime and editors both install it, so content
// validates without a game running. False when a declaration was refused.
[[nodiscard]] bool InstallPerceptionVocabulary(World& world);

// perception.stimulus stages into the World; the queries answer from its
// PerceptionFrame and its observers' memories. A World nobody scans answers
// Unavailable. Sight queries: true when seen this tick; false only when the
// observer's sight scan this tick was complete; otherwise Unavailable, never
// false. Memory queries: Unavailable off the authority or without a memory; a
// subject never observed is false for remembers and Unavailable for the rest.
class PerceptionOperations
{
public:
    explicit PerceptionOperations(World& world)
        : Entities(&world)
    {
    }

    SENCHA_VERB("perception.stimulus")
    SENCHA_LABEL("Perception stimulus")
    SENCHA_CATEGORY("Perception")
    SENCHA_DESCRIPTION("Something hearing picks up: a shot, a footstep, a hit. Heard by every ear within its radius, "
                       "or by one observer alone.")
    VerbAdmission Stimulus(
        SENCHA_ARG("source") SENCHA_LABEL("Source")
            SENCHA_DESCRIPTION("Who made it; an observer remembers them where it happened.") std::optional<EntityId> source,
        SENCHA_ARG("position") SENCHA_LABEL("Position")
            SENCHA_DESCRIPTION("Where it happened; the source's position without one.") std::optional<Vec3d> position,
        SENCHA_ARG("radius") SENCHA_LABEL("Radius") SENCHA_DESCRIPTION("Metres it carries to an ear of sensitivity one.")
            SENCHA_MIN(0) float radius,
        SENCHA_ARG("only") SENCHA_LABEL("Only")
            SENCHA_DESCRIPTION("The one observer it reaches, wherever that observer is.")
                std::optional<EntityId> only = std::nullopt);

    SENCHA_QUERY("perception.sees")
    SENCHA_LABEL("Sees")
    SENCHA_CATEGORY("Perception")
    SENCHA_DESCRIPTION("Whether the observer saw the subject this tick, by sight alone. Unavailable when its sight "
                       "scan did not finish.")
    std::optional<bool> Sees(SENCHA_ARG("observer") SENCHA_LABEL("Observer") EntityId observer,
                             SENCHA_ARG("subject") SENCHA_LABEL("Subject") EntityId subject) const;

    SENCHA_QUERY("perception.sees_tagged")
    SENCHA_LABEL("Sees tagged")
    SENCHA_CATEGORY("Perception")
    SENCHA_DESCRIPTION("Whether the observer saw anything carrying the tag, or a tag beneath it, this tick, by sight "
                       "alone. Unavailable when its sight scan did not finish.")
    std::optional<bool> SeesTagged(SENCHA_ARG("observer") SENCHA_LABEL("Observer") EntityId observer,
                                   SENCHA_ARG("tag") SENCHA_LABEL("Tag") GameplayTagId tag) const;

    SENCHA_QUERY("perception.nearest_seen_tagged")
    SENCHA_LABEL("Nearest seen tagged")
    SENCHA_CATEGORY("Perception")
    SENCHA_DESCRIPTION("The closest thing carrying the tag that the observer saw this tick, by sight alone.")
    std::optional<EntityId> NearestSeenTagged(SENCHA_ARG("observer") SENCHA_LABEL("Observer") EntityId observer,
                                              SENCHA_ARG("tag") SENCHA_LABEL("Tag") GameplayTagId tag) const;

    SENCHA_QUERY("perception.remembers")
    SENCHA_LABEL("Remembers")
    SENCHA_CATEGORY("Perception")
    SENCHA_DESCRIPTION("Whether the observer's memory holds the subject.")
    std::optional<bool> Remembers(SENCHA_ARG("observer") SENCHA_LABEL("Observer") EntityId observer,
                                  SENCHA_ARG("subject") SENCHA_LABEL("Subject") EntityId subject) const;

    SENCHA_QUERY("perception.last_seen_position")
    SENCHA_LABEL("Last seen position")
    SENCHA_CATEGORY("Perception")
    SENCHA_DESCRIPTION("Where the observer last sensed the subject, not where it is now.")
    std::optional<Vec3d> LastSeenPosition(SENCHA_ARG("observer") SENCHA_LABEL("Observer") EntityId observer,
                                          SENCHA_ARG("subject") SENCHA_LABEL("Subject") EntityId subject) const;

    SENCHA_QUERY("perception.last_seen_velocity")
    SENCHA_LABEL("Last seen velocity")
    SENCHA_CATEGORY("Perception")
    SENCHA_DESCRIPTION("The subject's velocity as the observer last measured it; zero when it could not.")
    std::optional<Vec3d> LastSeenVelocity(SENCHA_ARG("observer") SENCHA_LABEL("Observer") EntityId observer,
                                          SENCHA_ARG("subject") SENCHA_LABEL("Subject") EntityId subject) const;

    SENCHA_QUERY("perception.seconds_since_seen")
    SENCHA_LABEL("Seconds since seen")
    SENCHA_CATEGORY("Perception")
    SENCHA_DESCRIPTION("Seconds since the observer last sensed the subject.")
    std::optional<float> SecondsSinceSeen(SENCHA_ARG("observer") SENCHA_LABEL("Observer") EntityId observer,
                                          SENCHA_ARG("subject") SENCHA_LABEL("Subject") EntityId subject) const;

    SENCHA_QUERY("perception.seconds_tracked")
    SENCHA_LABEL("Seconds tracked")
    SENCHA_CATEGORY("Perception")
    SENCHA_DESCRIPTION("Seconds the observer has sensed the subject without a lapse.")
    std::optional<float> SecondsTracked(SENCHA_ARG("observer") SENCHA_LABEL("Observer") EntityId observer,
                                        SENCHA_ARG("subject") SENCHA_LABEL("Subject") EntityId subject) const;

    SENCHA_QUERY("perception.nearest_remembered_tagged")
    SENCHA_LABEL("Nearest remembered tagged")
    SENCHA_CATEGORY("Perception")
    SENCHA_DESCRIPTION("The closest remembered position of something alive carrying the tag.")
    std::optional<EntityId> NearestRememberedTagged(SENCHA_ARG("observer") SENCHA_LABEL("Observer") EntityId observer,
                                                    SENCHA_ARG("tag") SENCHA_LABEL("Tag") GameplayTagId tag) const;

private:
    World* Entities;
};

#if !defined(SENCHA_CODEGEN)
#  include <perception/PerceptionVocabulary.sencha.h>
#endif
