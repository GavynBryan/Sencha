#pragma once

#include <ecs/ComponentAnnotations.h>

struct SENCHA_COMPONENT("test.codegen.bad_runtime_without_default")
       SENCHA_SCHEMA("BadRuntimeWithoutDefault")
       SENCHA_SCENE_CHUNK("BRWD")
       SENCHA_SAVED
BadRuntimeWithoutDefault
{
    SENCHA_FIELD("seconds") SENCHA_UNSAVED float Seconds;
};
