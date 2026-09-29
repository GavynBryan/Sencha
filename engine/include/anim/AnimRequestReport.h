#pragma once

#include <anim/AnimTypes.h>
#include <ecs/ComponentAnnotations.h>

#include <cstdint>

// What became of an entity's requests, for anim.risk and the editor; nothing that
// plays reads it. NoteAnimRequestOutcomes writes it.
struct SENCHA_COMPONENT("sencha.anim_request_report") AnimRequestReport
{
    // Each request-set slot's retained request as last seen; the bit masks below are
    // per slot and clear when the slot's request changes.
    std::uint32_t Seen[kAnimRequestCapacity] = {};
    std::uint8_t Played = 0;
    // Seen held without its source or owner: once gives the producer a pass to cancel
    // it, twice reports it.
    std::uint8_t Unowned = 0;
    std::uint8_t Reported = 0;
    std::uint32_t Unplayed = 0;
    std::uint32_t Orphaned = 0;
};

static_assert(kAnimRequestCapacity <= 8, "The report holds one bit per request slot.");

#if !defined(SENCHA_CODEGEN)
#  include <anim/AnimRequestReport.sencha.h>
#endif
