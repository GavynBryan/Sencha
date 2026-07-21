#pragma once

#include <zone/ZoneParticipation.h>

#include <string>

class RuntimeWorld;
class WorldComponentSchema;
class ZoneLoadPackage;

struct ZoneImportError
{
    std::string Message;
};

// Owner-thread semantic kernel for publishing a detached package into a hidden
// RuntimeWorld partition. Any failure cancels the import completely; ordinary
// frame-domain systems can observe only the final published zone.
[[nodiscard]] bool ImportZonePackage(
    RuntimeWorld& runtime,
    const WorldComponentSchema& schema,
    const ZoneLoadPackage& package,
    ZoneParticipation participation = {},
    ZoneImportError* error = nullptr);
