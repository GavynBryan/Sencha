#pragma once

#include <authored/AuthoredAnnotations.h>
#include <authored/AuthoredApiDefinition.h>
#include <authored/VerbInvocation.h>
#include <ecs/EntityId.h>
#include <gameplay_tags/GameplayTagId.h>

#include <cstdint>
#include <optional>

class World;

// The claim lease and queries, declared into the World's catalogs with a
// ClaimBoard. Idempotent.
bool InstallClaimVocabulary(World& world);

class ClaimOperations
{
public:
    explicit ClaimOperations(World& world)
        : Entities(&world)
    {
    }

    SENCHA_LEASE("claims.hold")
    SENCHA_LABEL("Hold claim")
    SENCHA_CATEGORY("Claims")
    SENCHA_DESCRIPTION("Holds a place in a group's claim while held: a role, a token, a signal. Refused when every "
                       "place is taken.")
    VerbAdmission Hold(const VerbInvocation& invocation,
                       SENCHA_ARG("group") SENCHA_LABEL("Group") SENCHA_DESCRIPTION("The tag the group's members carry.")
                           GameplayTagId group,
                       SENCHA_ARG("claim") SENCHA_LABEL("Claim") SENCHA_DESCRIPTION("Which claim.") GameplayTagId claim,
                       SENCHA_ARG("count") SENCHA_LABEL("Count") SENCHA_DESCRIPTION("Places the claim holds.")
                           std::uint32_t count = 1,
                       SENCHA_ARG("share") SENCHA_LABEL("Share")
                           SENCHA_DESCRIPTION("Places as a share of the group's members, when that is more.")
                               SENCHA_MIN(0) float share = 0.0f);
    SENCHA_LEASE_RELEASE("claims.hold")
    VerbAdmission EndHold(const VerbInvocation& invocation);

    SENCHA_QUERY("claims.count")
    SENCHA_LABEL("Claim count")
    SENCHA_CATEGORY("Claims")
    SENCHA_DESCRIPTION("How many places in a group's claim are held.")
    std::optional<std::int64_t> Count(
        SENCHA_ARG("group") SENCHA_LABEL("Group") SENCHA_DESCRIPTION("The tag the group's members carry.")
            GameplayTagId group,
        SENCHA_ARG("claim") SENCHA_LABEL("Claim") SENCHA_DESCRIPTION("Which claim.") GameplayTagId claim) const;

    SENCHA_QUERY("claims.held")
    SENCHA_LABEL("Holds claim")
    SENCHA_CATEGORY("Claims")
    SENCHA_DESCRIPTION("Whether the entity holds a place in a group's claim.")
    std::optional<bool> Held(
        SENCHA_ARG("holder") SENCHA_LABEL("Holder") SENCHA_DESCRIPTION("Who might hold it.") EntityId holder,
        SENCHA_ARG("group") SENCHA_LABEL("Group") SENCHA_DESCRIPTION("The tag the group's members carry.")
            GameplayTagId group,
        SENCHA_ARG("claim") SENCHA_LABEL("Claim") SENCHA_DESCRIPTION("Which claim.") GameplayTagId claim) const;

    [[nodiscard]] const World& AuthoredWorld() const { return *Entities; }

private:
    World* Entities;
};

#if !defined(SENCHA_CODEGEN)
#  include <logic/ClaimVocabulary.sencha.h>
#endif
