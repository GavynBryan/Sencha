#pragma once

#include <authored/AuthoredAnnotations.h>
#include <authored/VerbInvocation.h>
#include <ecs/EntityId.h>

// Refused: a query observes, so it is const.
class BadMutableQuerySystem
{
public:
    SENCHA_QUERY("test.bad.count")
    int Count(SENCHA_TARGET("owner") EntityId owner);
};
