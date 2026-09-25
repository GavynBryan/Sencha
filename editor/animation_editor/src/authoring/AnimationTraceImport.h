#pragma once

#include <core/json/JsonValue.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

//=============================================================================
// Imported animation traces
//
// An animation.trace document (anim/AnimTrace.h) read back for display: one
// row per decision record, in the order the game logged them. A row keeps the
// record's tick, cause and layer, and describes every other field it carried
// -- including fields this editor does not know -- so a trace from a newer
// build still reads. A trace holds decisions only; it cannot be played back,
// and the reader says what it captured rather than filling a pose history in.
//=============================================================================

struct AnimationTraceRow
{
    std::uint64_t Tick = 0;
    std::string Cause;
    // The layer's name, or its index when the trace could not name it; empty
    // for a record about no one layer.
    std::string Layer;
    // Every other field, in the order the record carried them.
    std::string Detail;
};

struct AnimationTrace
{
    std::string Entity;
    // Empty when the game had no rig bound to name.
    std::string Rig;
    std::string Captured;
    // Every record the game ever logged for the entity; more than Rows when
    // its ring had overwritten the oldest before export.
    std::uint64_t RecordsWritten = 0;
    std::vector<AnimationTraceRow> Rows;

    [[nodiscard]] std::uint64_t Overwritten() const
    {
        return RecordsWritten > Rows.size() ? RecordsWritten - Rows.size() : 0;
    }
};

[[nodiscard]] std::optional<AnimationTrace> ReadAnimationTrace(const JsonValue& document, std::string& error);
[[nodiscard]] std::optional<AnimationTrace> ReadAnimationTraceFile(const std::filesystem::path& file,
                                                                   std::string& error);
