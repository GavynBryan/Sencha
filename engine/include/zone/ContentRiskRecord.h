#pragma once

#include <cstdint>
#include <string>

// The structured content warning record, minimal on purpose: streaming records
// will extend the family; coordinate rather than duplicate when they land.
enum class ContentRiskSeverity : uint8_t
{
    Warning,
    Error,
};

enum class ContentRiskSourceKind : uint8_t
{
    World,
    Graph,
    Zone,
    Dock,
    Link,
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
