#pragma once

#include <ecs/ComponentAnnotations.h>

struct SENCHA_COMPONENT("test.codegen.bad_chunk")
       SENCHA_SCHEMA("BadShortChunk")
       SENCHA_SCENE_CHUNK("NOPE!")
BadShortChunk
{
    SENCHA_FIELD("x") int X = 0;
};
