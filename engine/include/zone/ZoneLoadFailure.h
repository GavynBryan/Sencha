#pragma once

#include <zone/ZoneId.h>

#include <cstdint>
#include <string>

// Which stage of a zone load refused. Stages run in this order and a load
// reports the first one that refused it.
//
// Build failures exist only on the scene-driven path, whose worker stages and
// parses the cooked scene and so has real ways to refuse (missing file,
// corrupt image, schema skew). The raw BuildFn path has no failure channel --
// it returns void under the async lane's no-throw contract -- so its missing
// or corrupt content surfaces as a Finalize refusal instead.
enum class ZoneLoadStage : std::uint8_t
{
    Build,     // scene staging or package construction failed
    Import,    // package import into the hidden partition failed
    Finalize,  // the recipe's finalize callback declined publication
    Publish,   // publication of an imported partition failed
};

[[nodiscard]] inline const char* ZoneLoadStageName(ZoneLoadStage stage)
{
    switch (stage)
    {
    case ZoneLoadStage::Build:    return "build";
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
