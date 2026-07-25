#pragma once

#include <zone/ZoneId.h>

#include <cstdint>
#include <string>

// Which owner-thread stage of a zone load refused. Stages run in this order and a
// load reports the first one that refused it.
//
// There is deliberately no Build stage: the async lane enforces a no-throw
// contract and BuildFn returns void, so worker-side package construction has no
// failure channel. Missing or corrupt content therefore surfaces as a Finalize
// refusal, which is what a recipe's finalize callback is for.
enum class ZoneLoadStage : std::uint8_t
{
    Import,    // package import into the hidden partition failed
    Finalize,  // the recipe's finalize callback declined publication
    Publish,   // publication of an imported partition failed
};

[[nodiscard]] inline const char* ZoneLoadStageName(ZoneLoadStage stage)
{
    switch (stage)
    {
    case ZoneLoadStage::Import:   return "import";
    case ZoneLoadStage::Finalize: return "finalize";
    case ZoneLoadStage::Publish:  return "publish";
    }
    return "unknown";
}

// One zone load that refused. Recorded rather than dropped so a broken zone is
// observable, and so the streaming policy can stop re-issuing it: demand alone
// cannot distinguish "not loaded yet" from "will never load".
struct ZoneLoadFailure
{
    ZoneId        Zone;
    ZoneLoadStage Stage = ZoneLoadStage::Import;
    std::string   Message;
};
