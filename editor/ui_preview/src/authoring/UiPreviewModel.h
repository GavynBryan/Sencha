#pragma once

#include <core/json/JsonValue.h>
#include <ui/UiScreenDesc.h>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class UiService;

// The model a document is previewed against: what a host would declare for it
// and the values it would publish, kept beside the document as
// `<name>.preview.json` (docs/ui/architecture.md, The preview model).
struct UiPreviewModel
{
    std::string ModelName;
    bool Modal = false;
    // Initial carries the sample value.
    std::vector<UiModelProperty> Properties;

    struct ArraySample
    {
        std::string Name;
        std::vector<std::string> Items;

        bool operator==(const ArraySample&) const = default;
    };
    std::vector<ArraySample> Arrays;

    struct RowsSample
    {
        std::string Name;
        std::vector<UiRow> Items;

        bool operator==(const RowsSample&) const = default;
    };
    std::vector<RowsSample> Rows;

    std::vector<std::string> Actions;

    // What a new row list starts with.
    [[nodiscard]] static UiRow SampleRow() { return UiRow{ "Row", "value", "" }; }

    // `pause.rml` -> `pause.preview.json`, beside it.
    [[nodiscard]] static std::filesystem::path SidecarFor(const std::filesystem::path& documentSource);

    [[nodiscard]] static std::optional<UiPreviewModel> Parse(const JsonValue& json, std::string* error);
    [[nodiscard]] static std::optional<UiPreviewModel> ParseText(std::string_view text, std::string* error);
    [[nodiscard]] static std::optional<UiPreviewModel> Load(const std::filesystem::path& sidecar,
                                                            std::string* error);
    [[nodiscard]] JsonValue ToJson() const;
    // The sidecar's text; false when a value cannot be written.
    [[nodiscard]] bool WriteText(std::string& text, std::string* error) const;
    bool Save(const std::filesystem::path& sidecar, std::string* error) const;

    // The declaration a host would make for the package at `packagePath`.
    [[nodiscard]] UiScreenDesc Describe(std::string packagePath) const;

    // Publishes the sample lists and rows to an open screen. Properties need no
    // publishing: their samples went in as the declaration's initial values.
    void Publish(UiService& ui, UiScreenHandle screen) const;

    [[nodiscard]] bool DeclaresProperty(std::string_view name) const;
    [[nodiscard]] bool DeclaresArray(std::string_view name) const;
    [[nodiscard]] bool DeclaresRows(std::string_view name) const;
    [[nodiscard]] bool DeclaresAction(std::string_view name) const;

    // The same declarations and samples, as a saved sidecar would compare.
    [[nodiscard]] bool operator==(const UiPreviewModel& other) const;
};
