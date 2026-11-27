#pragma once

#include <abilities/AbilityGrants.h>
#include <abilities/AbilityId.h>
#include <authored/AuthoredAnnotations.h>
#include <authored/AuthoredApiDefinition.h>
#include <authored/VerbInvocation.h>
#include <ecs/EntityId.h>

#include <optional>
#include <string_view>

class World;

class AbilityOperations
{
public:
    explicit AbilityOperations(World& world)
        : Entities(&world)
    {
    }

    SENCHA_VERB("ability.activate")
    SENCHA_LABEL("Activate ability")
    SENCHA_CATEGORY("Abilities")
    SENCHA_DESCRIPTION("Starts an ability the actor is granted, if admission allows, in the next ability pass; with a "
                       "source, only the grant that source supplied.")
    VerbAdmission Activate(const VerbInvocation& invocation,
                           SENCHA_TARGET("target", AbilityGrants) SENCHA_LABEL("Target") SENCHA_DESCRIPTION("The actor.")
                               EntityId target,
                           SENCHA_ARG("ability") SENCHA_LABEL("Ability") AbilityId ability,
                           SENCHA_ARG("source") SENCHA_LABEL("Source")
                               SENCHA_DESCRIPTION("What the grant comes from, as its activations read it.")
                                   std::optional<EntityId> source = std::nullopt);

    SENCHA_VERB("ability.cancel")
    SENCHA_LABEL("Cancel ability")
    SENCHA_CATEGORY("Abilities")
    SENCHA_DESCRIPTION("Ends the actor's running activations of an ability.")
    VerbAdmission Cancel(const VerbInvocation& invocation,
                         SENCHA_TARGET("target", AbilityGrants) SENCHA_LABEL("Target") SENCHA_DESCRIPTION("The actor.")
                             EntityId target,
                         SENCHA_ARG("ability") SENCHA_LABEL("Ability") AbilityId ability);

    SENCHA_VERB("ability.grant")
    SENCHA_LABEL("Grant ability")
    SENCHA_CATEGORY("Abilities")
    SENCHA_DESCRIPTION("Lets the actor run an ability, under a key.")
    VerbAdmission Grant(SENCHA_TARGET("target", AbilityGrants) SENCHA_LABEL("Target") SENCHA_DESCRIPTION("The actor.")
                            EntityId target,
                        SENCHA_ARG("ability") SENCHA_LABEL("Ability") AbilityId ability,
                        SENCHA_ARG("key") SENCHA_LABEL("Key") SENCHA_DESCRIPTION("Names the grant, so it can be revoked.")
                            std::string_view key,
                        SENCHA_ARG("source") SENCHA_LABEL("Source")
                            SENCHA_DESCRIPTION("What the grant comes from, as its activations read it.")
                                std::optional<EntityId> source = std::nullopt);

    SENCHA_VERB("ability.revoke")
    SENCHA_LABEL("Revoke ability")
    SENCHA_CATEGORY("Abilities")
    SENCHA_DESCRIPTION("Takes back an ability granted under a key.")
    VerbAdmission Revoke(SENCHA_TARGET("target", AbilityGrants) SENCHA_LABEL("Target") SENCHA_DESCRIPTION("The actor.")
                             EntityId target,
                         SENCHA_ARG("ability") SENCHA_LABEL("Ability") AbilityId ability,
                         SENCHA_ARG("key") SENCHA_LABEL("Key") SENCHA_DESCRIPTION("Names the grant, so it can be revoked.")
                             std::string_view key);

    [[nodiscard]] const World& AuthoredWorld() const { return *Entities; }

private:
    World* Entities;
};

#if !defined(SENCHA_CODEGEN)
#  include <abilities/AbilityVerbs.sencha.h>
#endif
