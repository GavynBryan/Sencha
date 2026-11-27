#pragma once

#include <ecs/ComponentAnnotations.h>

struct SENCHA_COMPONENT("test.codegen.bad_runtime_and_unsaved")
       SENCHA_SCHEMA("BadRuntimeAndUnsaved")
       SENCHA_SCENE_CHUNK("BRAU")
       SENCHA_SAVED
BadRuntimeAndUnsaved
{
    SENCHA_FIELD("engaged") SENCHA_RUNTIME SENCHA_UNSAVED bool Engaged = false;
};
