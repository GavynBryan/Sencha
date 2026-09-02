#pragma once

#include <ecs/ComponentAnnotations.h>

// Annotated, but never says what it is called.
struct SENCHA_SCHEMA("BadNoIdentity") BadNoIdentity
{
    SENCHA_FIELD("x") int X = 0;
};
