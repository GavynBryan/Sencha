#pragma once

#include <authored/AuthoredAnnotations.h>
#include <authored/AuthoredApiDefinition.h>
#include <authored/VerbInvocation.h>
#include <ecs/EntityId.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagId.h>

#include <cstdint>

class World;

class GameplayTagOperations
{
public:
    explicit GameplayTagOperations(World& world)
        : Entities(&world)
    {
    }

    SENCHA_VERB("tags.grant")
    SENCHA_LABEL("Grant tag")
    SENCHA_CATEGORY("Tags")
    SENCHA_DESCRIPTION("Gives the target stacks of a tag it keeps and saves; an entity with no tags gains them.")
    VerbAdmission Grant(
        SENCHA_TARGET("target", GameplayTagContainer) SENCHA_LABEL("Target")
            SENCHA_DESCRIPTION("The entity that keeps it.") EntityId target,
        SENCHA_ARG("tag") SENCHA_LABEL("Tag") GameplayTagId tag,
        SENCHA_ARG("stacks") SENCHA_LABEL("Stacks") SENCHA_DESCRIPTION("How many stacks to add.")
            SENCHA_RANGE(1, 65535) std::uint16_t stacks = 1);

    SENCHA_VERB("tags.revoke")
    SENCHA_LABEL("Revoke tag")
    SENCHA_CATEGORY("Tags")
    SENCHA_DESCRIPTION("Takes stacks of a tag the target keeps; never what an effect or ability holds.")
    VerbAdmission Revoke(
        SENCHA_TARGET("target", GameplayTagContainer) SENCHA_LABEL("Target")
            SENCHA_DESCRIPTION("The entity that keeps it.") EntityId target,
        SENCHA_ARG("tag") SENCHA_LABEL("Tag") GameplayTagId tag,
        SENCHA_ARG("stacks") SENCHA_LABEL("Stacks") SENCHA_DESCRIPTION("How many stacks to take, at most what it keeps.")
            SENCHA_RANGE(1, 65535) std::uint16_t stacks = 1);

    [[nodiscard]] const World& AuthoredWorld() const { return *Entities; }

private:
    [[nodiscard]] bool Writable(GameplayTagId tag) const;

    World* Entities;
};

#if !defined(SENCHA_CODEGEN)
#  include <gameplay_tags/GameplayTagVerbs.sencha.h>
#endif
