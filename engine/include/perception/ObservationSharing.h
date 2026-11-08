#pragma once

#include <core/text/InlineString.h>
#include <ecs/ComponentAnnotations.h>

// Puts an observer in a group whose members share what they sense: each
// observation a member makes reaches every other member's ObservationMemory,
// under the shared sense, after a delay and within a radius of the member
// that made it. A radio net, a pack, a hive. Authority only.
struct SENCHA_COMPONENT("sencha.observation_sharing")
       SENCHA_SCHEMA("ObservationSharing")
       SENCHA_SCENE_CHUNK("OBSS")
ObservationSharing
{
    SENCHA_FIELD("group")
    SENCHA_TOOLTIP("The gameplay tag naming the group whose members share.")
    InlineString<64> Group;

    SENCHA_FIELD("radius")
    SENCHA_OPTIONAL
    SENCHA_LABEL("Radius")
    SENCHA_TOOLTIP("How far from the member that sensed it an observation reaches; zero reaches every member.")
    float Radius = 0.0f;

    SENCHA_FIELD("delay_seconds")
    SENCHA_OPTIONAL
    SENCHA_LABEL("Delay seconds")
    SENCHA_TOOLTIP("How long an observation takes to reach the other members.")
    float DelaySeconds = 0.0f;
};

#if !defined(SENCHA_CODEGEN)
#  include <perception/ObservationSharing.sencha.h>
#endif
