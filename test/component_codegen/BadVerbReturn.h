#pragma once

#include <authored/AuthoredAnnotations.h>
#include <authored/VerbInvocation.h>
#include <ecs/EntityId.h>

// Refused: a verb answers whether it admitted the request.
class BadVerbReturnSystem
{
public:
    SENCHA_VERB("test.bad.jump")
    bool Jump();
};
