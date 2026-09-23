#pragma once

#include <authored/AuthoredAnnotations.h>
#include <authored/VerbInvocation.h>
#include <ecs/EntityId.h>

// Generated, then refused where the companion is compiled: a type that exists
// but has no component identity cannot constrain a target or an event source.
struct BadNotAComponent
{
    int Value = 0;
};

class BadNonComponentTargetSystem
{
public:
    SENCHA_VERB("test.bad.poke")
    VerbAdmission Poke(SENCHA_TARGET("thing", BadNotAComponent) EntityId thing);
};

struct SENCHA_EVENT("test.bad.poked") SENCHA_EVENT_SOURCE(BadNotAComponent) BadPokedEvent
{
};
