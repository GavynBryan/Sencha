#pragma once

#include <ecs/ComponentAnnotations.h>

// Where the level says a player begins. The identity is the one every starter
// template uses, so a level authored in one opens in any other; what a game
// does at a start is its own.
struct SENCHA_COMPONENT("player_start")
       SENCHA_SCHEMA("player_start")
       SENCHA_SCENE_CHUNK("PSTR")
PlatformerStart
{
};

#if !defined(SENCHA_CODEGEN)
#  include <PlatformerStart.sencha.h>
#endif
