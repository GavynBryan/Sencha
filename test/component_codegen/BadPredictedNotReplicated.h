#pragma once

#include <ecs/ComponentAnnotations.h>

// A client resumes from a value that only arrives if the component travels.
struct SENCHA_COMPONENT("test.codegen.bad_predicted")
       SENCHA_SCHEMA("BadPredicted")
       SENCHA_PREDICTED
BadPredictedNotReplicated
{
    SENCHA_FIELD("x") int X = 0;
};
