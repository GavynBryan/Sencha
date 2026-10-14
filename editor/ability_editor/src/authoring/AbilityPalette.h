#pragma once

#include "authoring/AbilityElements.h"
#include "workspaces/TreeOutline.h"

#include <core/json/JsonValue.h>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class VocabularyCatalog;

// Something an op can run: a verb, a lease or a query the project's names
// declare, or one of the kit's own ops (set a local, commit costs, cue).
struct AbilityPaletteEntry
{
    // "verb:effect.apply", "lease:time.scale", "query:physics.trace", "kit:set".
    std::string Key;
    std::string Label;
    std::string Group;
    // Its scope and where it may be predicted, for a catalog entry.
    std::string Badges = {};
};

[[nodiscard]] std::vector<AbilityPaletteEntry> AbilityPaletteOf(const VocabularyCatalog& names);
// Grouped under their kinds, keyed by entry; a group's key starts with "group:".
[[nodiscard]] std::vector<OutlineNode> AbilityPaletteNodes(const std::vector<AbilityPaletteEntry>& entries);

// The op an entry makes, unkeyed; null for a key no entry has.
[[nodiscard]] JsonValue AbilityPaletteOp(std::string_view entry);

struct AbilityOpPlacement
{
    std::string Phase;
    AbilityOpList List = AbilityOpList::Enter;
    std::size_t Index = 0;
};

// Where a new op goes: the strip slot `slot` names, else just after the
// selected op, else the end of the selected phase's entry list; a lease always
// to its phase's held list, and nothing else there. None with no phase to put it in.
[[nodiscard]] std::optional<AbilityOpPlacement> AbilityOpPlacementFor(const JsonValue& data, std::string_view slot,
                                                                      std::string_view selected, bool lease);
