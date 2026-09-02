#pragma once

#include <ecs/ComponentAnnotations.h>

// Marks where the player avatar spawns: place it on an entity that carries a
// Transform (world scene content on the world path; any scene works). The
// position is the entity's transform, so the component itself is a zero-size
// tag. Game-defined like SpinComponent: its schema makes it cook and edit
// without any editor code naming it.
struct SENCHA_COMPONENT("player_start")
       SENCHA_SCHEMA("player_start")
       SENCHA_SCENE_CHUNK("PSTR")
PlayerStartComponent
{
};

#if !defined(SENCHA_CODEGEN)
#  include <PlayerStartComponent.sencha.h>
#endif
