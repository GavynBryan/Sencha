#pragma once

#include <core/json/JsonValue.h>
#include <ui/UiScreenDesc.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

class UiService;

//=============================================================================
// UiPreviewModel
//
// The model a document is previewed against: what a host would declare for it
// (UiScreenDesc, minus the package path) and the values that host would
// publish -- as a file beside the document, `<name>.preview.json`.
//
// The sidecar is the authoritative preview schema. It travels with the
// document, so a second author sees it rendered rather than blank; a test
// opens the document with it, so preview and test cannot drift; a template
// ships its UI with one, which is documentation a new author reads by opening
// it. Diagnostics observed at runtime help write it incrementally
// (BindingMisses); they never replace it.
//
// Values are written with an explicit kind. "1", 1 and 1.0 are three different
// presentation values, and a document that compares one of them to a literal
// cares which.
//=============================================================================
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
    };
    std::vector<ArraySample> Arrays;

    struct RowsSample
    {
        std::string Name;
        std::vector<UiRow> Items;
    };
    std::vector<RowsSample> Rows;

    std::vector<std::string> Actions;

    // `pause.rml` -> `pause.preview.json`, beside it.
    [[nodiscard]] static std::filesystem::path SidecarFor(const std::filesystem::path& documentSource);

    [[nodiscard]] static std::optional<UiPreviewModel> Parse(const JsonValue& json, std::string* error);
    [[nodiscard]] static std::optional<UiPreviewModel> Load(const std::filesystem::path& sidecar,
                                                            std::string* error);
    [[nodiscard]] JsonValue ToJson() const;
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
};

// The document-facing names of a row's control, as a document compares them
// (`row.control == 'range'`). Shared with the sidecar so the two spellings
// cannot part.
[[nodiscard]] const char* UiRowControlName(UiRowControl control);
[[nodiscard]] std::optional<UiRowControl> ParseUiRowControl(std::string_view name);
