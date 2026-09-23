#pragma once

#include <authored/AuthoredAnnotations.h>
#include <authored/VerbInvocation.h>
#include <ecs/EntityId.h>

// Refused: one name, two contracts.
class BadDuplicateIdentitySystem
{
public:
    SENCHA_VERB("test.bad.fire")
    VerbAdmission Fire();

    SENCHA_VERB("test.bad.fire")
    VerbAdmission FireAgain();
};
