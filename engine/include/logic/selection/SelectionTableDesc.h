#pragma once

#include <core/json/JsonValue.h>
#include <core/metadata/DataSchema.h>
#include <gameplay_tags/GameplayTagQueryNames.h>
#include <math/ConsiderationRule.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// A table compiled with no World, shared by every World that binds it.
// Authored form: docs/gameplay/selection.md.

inline constexpr std::string_view kSelectionTableTypeName = "selection.table";
inline constexpr std::uint32_t kMaxSelectionOptions = 64;
inline constexpr std::uint32_t kMaxSelectionConsiderations = 32;

enum class SelectionSource : std::uint8_t
{
    // A number the caller supplies in the table's input order.
    Input,
    // An authored query whose numeric answer is the value.
    Query,
};

enum class SelectionMode : std::uint8_t
{
    Best,
    PickFromBand,
};

// A query argument that takes one of the table's entities rather than a constant.
struct SelectionEntityArgument
{
    std::string Argument;
    std::uint8_t Entity = 0;
};

struct SelectionConsiderationDesc
{
    SelectionSource Source = SelectionSource::Input;
    std::uint8_t Input = 0;
    std::string Query;
    std::vector<SelectionEntityArgument> EntityArguments;
    JsonValue Constants;

    ConsiderationRule Rule;
};

struct SelectionOptionDesc
{
    std::string Name;
    // Once chosen, the option is kept this long unless the table is interrupted.
    float CommitSeconds = 0.0f;
    std::vector<SelectionConsiderationDesc> Considerations;
};

struct SelectionTableDesc
{
    std::string Name;
    // Index 0 is the querier; authored entities follow in authored order.
    std::vector<std::string> Entities;
    std::vector<std::string> Inputs;
    std::vector<SelectionOptionDesc> Options;
    // Another option replaces the current one only when it scores this much more.
    float SwitchMargin = 0.0f;
    SelectionMode Mode = SelectionMode::Best;
    float BandWidth = 0.0f;
    // Tags on the querier that end a commitment early.
    GameplayTagQueryNames Interrupt;
};

// Collects every problem before returning false.
[[nodiscard]] bool CompileSelectionTableDesc(const JsonValue& data, SelectionTableDesc& out,
                                             std::vector<std::string>& errors);

[[nodiscard]] DataSchema MakeSelectionTableSchema();
