#pragma once

#include <assets/data/DataAssetHandle.h>
#include <authored/AuthoredQueryCall.h>
#include <authored/AuthoredQueryRegistry.h>
#include <authored/BindingDependencyStamp.h>
#include <gameplay_tags/GameplayTagQuery.h>
#include <logic/selection/SelectionTableDesc.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class DataAssetCache;
class GameplayTagRegistry;

struct SelectionConsideration
{
    SelectionSource Source = SelectionSource::Input;
    std::uint8_t Input = 0;
    AuthoredQueryCall Call;
    // Argument slots the table's entities fill, beside the entity each takes.
    std::vector<std::size_t> EntitySlots;
    std::vector<std::uint8_t> Entities;

    ConsiderationRule Rule;
};

struct SelectionOption
{
    std::string Name;
    float CommitSeconds = 0.0f;
    std::vector<SelectionConsideration> Considerations;
};

// A table bound into one World: queries resolved, the interrupt tags resolved.
class SelectionTable
{
public:
    [[nodiscard]] std::string_view Name() const { return Name_; }
    [[nodiscard]] std::span<const std::string> Entities() const { return Entities_; }
    [[nodiscard]] std::span<const std::string> Inputs() const { return Inputs_; }
    [[nodiscard]] std::span<const SelectionOption> Options() const { return Options_; }
    [[nodiscard]] float SwitchMargin() const { return SwitchMargin_; }
    [[nodiscard]] SelectionMode Mode() const { return Mode_; }
    [[nodiscard]] float BandWidth() const { return BandWidth_; }
    // Null when the table names no interrupt.
    [[nodiscard]] const GameplayTagQuery* Interrupt() const { return HasInterrupt ? &Interrupt_ : nullptr; }

    [[nodiscard]] std::optional<std::uint8_t> FindEntity(std::string_view name) const;
    [[nodiscard]] std::optional<std::uint8_t> FindInput(std::string_view name) const;
    [[nodiscard]] std::optional<std::uint32_t> FindOption(std::string_view name) const;

private:
    friend bool BindSelectionTable(const SelectionTableDesc&, const AuthoredQueryRegistry*,
                                   const GameplayTagRegistry*, SelectionTable&, std::vector<std::string>&);

    std::string Name_;
    std::vector<std::string> Entities_;
    std::vector<std::string> Inputs_;
    std::vector<SelectionOption> Options_;
    float SwitchMargin_ = 0.0f;
    SelectionMode Mode_ = SelectionMode::Best;
    float BandWidth_ = 0.0f;
    GameplayTagQuery Interrupt_;
    bool HasInterrupt = false;
};

// Owner thread. Collects every problem before returning false.
[[nodiscard]] bool BindSelectionTable(const SelectionTableDesc& desc,
                                      const AuthoredQueryRegistry* queries,
                                      const GameplayTagRegistry* tags,
                                      SelectionTable& out,
                                      std::vector<std::string>& errors);

// A `selection.table` asset bound into one World, rebound when the asset, the
// query catalog or the tag vocabulary moves. Hold this, never a cache pointer.
class SelectionTableBinding
{
public:
    bool BindFrom(const DataAssetCache& cache, DataAssetHandle asset, const AuthoredQueryRegistry* queries,
                  const GameplayTagRegistry* tags, std::vector<std::string>& errors);

    // True when it rebound. A few integer comparisons when nothing moved.
    bool Refresh(const DataAssetCache& cache, const AuthoredQueryRegistry* queries, const GameplayTagRegistry* tags,
                 std::vector<std::string>& errors);

    // Null when the last bind failed.
    [[nodiscard]] const SelectionTable* Table() const { return Bound.has_value() ? &*Bound : nullptr; }

private:
    DataAssetStamp Asset;
    CatalogStamp Queries;
    TagVocabularyStamp Tags;
    std::optional<SelectionTable> Bound;
};
