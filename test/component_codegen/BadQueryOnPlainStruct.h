#pragma once

#include <authored/AuthoredAnnotations.h>
#include <authored/VerbInvocation.h>
#include <ecs/EntityId.h>

// Refused: a member query reads a component, and this is not one.
struct BadPlainState
{
    SENCHA_QUERY("grounded")
    bool Grounded = false;
};
