#pragma once

#include <assets/data/DataAssetTypeRegistry.h>
#include <authored/VerbBinding.h>
#include <core/assets/AssetRef.h>
#include <core/json/JsonValue.h>
#include <core/metadata/DataSchema.h>

#include <array>
#include <string>
#include <string_view>
#include <vector>

inline constexpr std::string_view kCuePresentationType = "cue.presentation";

// What presenting a cue may read, as a binding names it with {"port": ...}.
inline constexpr std::array<std::string_view, 6> kCuePresentationPorts{
    "actor", "target", "point", "normal", "seed", "count",
};

// One cue's presentation: cosmetic verbs run once per occurrence, and cosmetic
// leases held for as long as a phase presents it. Bindings are named, not yet
// resolved; a World compiles them (CuePresentationLibrary).
struct CuePresentationEntry
{
    std::string Key;
    std::string Cue;
    std::vector<VerbBindingDesc> Once;
    std::vector<VerbBindingDesc> Sustain;
};

struct CuePresentationDeclaration
{
    std::vector<CuePresentationEntry> Entries;
};

void RegisterCuePresentationData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas);

// Reads `data` into a declaration, needing no World; asset references are
// appended to `dependencies`. True when nothing was malformed.
[[nodiscard]] bool CompileCuePresentation(const JsonValue& data, CuePresentationDeclaration& out,
                                          std::vector<AssetRef>& dependencies, std::vector<std::string>& errors);
