#pragma once

#include <cstdint>
#include <string>

// The structured content warning record, minimal on purpose: streaming records
// will extend the family; coordinate rather than duplicate when they land.
enum class ContentRiskSeverity : uint8_t
{
    Warning,
    Error,
    // The rule's inputs are not loaded (e.g. a portal check whose source zone
    // is closed): neither a verdict nor a silence. Coordination note: if the
    // streaming telemetry records land with their own severity vocabulary,
    // adopt it here and delete this note.
    Unverified,
};

enum class ContentRiskSourceKind : uint8_t
{
    World,
    Region,
    Zone,
    Transition,
};

struct ContentRiskRecord
{
    ContentRiskSeverity   Severity = ContentRiskSeverity::Warning;
    ContentRiskSourceKind Kind = ContentRiskSourceKind::World;
    uint64_t              SourceId = 0;   // offending record's id value; 0 for world-level
    std::string           RuleId;         // stable, dotted rule identifier
    std::string           Message;        // human-readable, names ids in hex
};
