#pragma once

#include <authored/AuthoredAnnotations.h>

#include <cstdint>

// The match's score, on the match entity, replicated to everyone.
//
// A component rather than a resource because the score is state other peers
// see: a late joiner gets it from the snapshot like any other replicated
// value, not by replaying every award that ever happened. The verb that awards
// points is a request; this is what the request changed. Both sides are
// readable by authored content as arena_scoreboard.red and .blue.
struct SENCHA_COMPONENT("arena_scoreboard")
       SENCHA_SCHEMA("arena_scoreboard")
       SENCHA_REPLICATED
ArenaScoreboard
{
    SENCHA_FIELD("red")
    SENCHA_QUERY("red")
    SENCHA_LABEL("Red")
    std::int32_t Red = 0;

    SENCHA_FIELD("blue")
    SENCHA_QUERY("blue")
    SENCHA_LABEL("Blue")
    std::int32_t Blue = 0;
};

#if !defined(SENCHA_CODEGEN)
#  include <src/ArenaScoreboard.sencha.h>
#endif
