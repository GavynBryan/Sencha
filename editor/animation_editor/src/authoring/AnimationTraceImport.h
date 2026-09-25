#pragma once

#include <core/json/JsonValue.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// An animation.trace read back for display, one row per record. Fields this
// editor does not know are shown, so a newer build's trace still reads.

struct AnimationTraceRow
{
    std::uint64_t Tick = 0;
    std::string Cause;
    // Name, else index; empty for a record not about one layer.
    std::string Layer;
    // Every other field, in record order.
    std::string Detail;
};

struct AnimationTrace
{
    std::string Entity;
    std::string Rig;
    std::string Captured;
    // Exceeds Rows.size() when the ring overwrote records before export.
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
