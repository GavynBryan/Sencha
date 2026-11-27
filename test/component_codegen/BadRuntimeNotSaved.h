#pragma once

#include <ecs/ComponentAnnotations.h>

struct SENCHA_COMPONENT("test.codegen.bad_runtime_not_saved")
       SENCHA_SCHEMA("BadRuntimeNotSaved")
       SENCHA_SCENE_CHUNK("BRNS")
BadRuntimeNotSaved
{
    SENCHA_FIELD("engaged") SENCHA_RUNTIME bool Engaged = false;
};
