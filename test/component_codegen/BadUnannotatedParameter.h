#pragma once

#include <authored/AuthoredAnnotations.h>
#include <authored/VerbInvocation.h>
#include <ecs/EntityId.h>

// Refused: every authored argument is named by the content that supplies it.
class BadUnannotatedParameterSystem
{
public:
    SENCHA_VERB("test.bad.push")
    VerbAdmission Push(SENCHA_ARG("force") float force, float scale);
};
