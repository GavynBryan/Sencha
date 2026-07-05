#pragma once

#include <ecs/EntityId.h>
#include <zone/WorldPartitionIds.h>
#include <zone/WorldPartitionManifest.h>
#include <zone/ZoneId.h>

#include <string>

class CommandStack;
class WorldDocument;

// The transition's user-facing label: its authored Name, else the derived
// "<From name> -> <To name>". Every surface (rows, inspector, connect bar)
// names the edge the same way, and always as world-level data.
[[nodiscard]] std::string TransitionDisplayName(const WorldPartitionManifest& manifest,
                                                const TransitionRecord& record);

// The one zone-connect flow every UI entry point routes through: mints a
// Doorway transition from `from` to `to` with default priority (plus the
// reverse edge unless oneWay), links `portal` when it names a portal entity in
// the focus scene (an undoable command on the stack; the manifest edges are
// verbs and stay non-undoable), and revalidates. Returns the forward edge id
// for row highlighting; invalid when refused (not world mode, from == to, or
// an unknown zone).
[[nodiscard]] TransitionId ConnectZones(WorldDocument& world, ZoneId from, ZoneId to,
                                        bool oneWay, EntityId portal,
                                        CommandStack& commands);
