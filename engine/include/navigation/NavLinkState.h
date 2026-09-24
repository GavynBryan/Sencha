#pragma once

#include <ecs/ComponentAnnotations.h>
#include <navigation/NavLinkComponent.h>
#include <navigation/NavigationIds.h>

// Gameplay-owned state of one navigation link, carried by whatever entity
// decides it -- a door, a lift, a bridge. Navigation reads it once per fixed
// tick and applies it to the link's zone, so the pathfinder never calls into
// gameplay and every query within a tick sees the same state. A link no entity
// names is enabled at its authored cost.
//
// Because this is ordinary component data it can take part in replication and
// in save persistence through the normal component machinery. Neither happens
// by itself: the component is not marked replicated (clients do not compute
// authoritative routes), and saving waits for the save system.
struct SENCHA_COMPONENT("sencha.navigation.link_state")
       SENCHA_SCHEMA("Nav Link State")
       SENCHA_SCENE_CHUNK("NLST")
NavLinkState
{
    SENCHA_FIELD("link")
    NavLinkId Link;

    SENCHA_FIELD("enabled")
    bool Enabled = true;

    // Multiplies the link's crossing cost; 1 leaves it as authored.
    SENCHA_FIELD("cost_scale")
    float CostScale = 1.0f;
};

#if !defined(SENCHA_CODEGEN)
#  include <navigation/NavLinkState.sencha.h>
#endif
