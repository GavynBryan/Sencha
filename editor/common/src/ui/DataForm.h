#pragma once

#include <core/json/JsonValue.h>

#include <string>
#include <string_view>
#include <vector>

struct DataFieldSchema;
class DataDocument;

// What a widget did to the working value this frame. A drag reports Changed on
// every frame it moves and Committed once when it ends, so it leaves one undo
// step; instant widgets (checkboxes, picks, structural buttons) report both.
struct FieldEdit
{
    bool Changed = false;
    bool Committed = false;

    FieldEdit& operator|=(const FieldEdit& other)
    {
        Changed = Changed || other.Changed;
        Committed = Committed || other.Committed;
        return *this;
    }

    [[nodiscard]] static FieldEdit Instant() { return FieldEdit{ true, true }; }
};

class DataFormHost
{
public:
    virtual ~DataFormHost() = default;

    // An empty `subtype` lists every data asset.
    [[nodiscard]] virtual std::vector<std::string> DataAssetPaths(std::string_view subtype) = 0;
    virtual void OpenDataAsset(std::string_view path) = 0;
    virtual void SelectField(const DataFieldSchema& field, std::string_view path) = 0;
    virtual void EditPreviewed(DataDocument& document) = 0;
    virtual void EditCommitted(DataDocument& document) = 0;
};

[[nodiscard]] std::string DataFieldDisplayName(const DataFieldSchema& field);

// Opens the document's edit scope on the first change, previews while the
// interaction runs, and commits when it ends.
void ApplyFieldEdit(DataDocument& document, DataFormHost& host, const FieldEdit& edit, JsonValue root);

// The schema-generated form for one field and everything under it. A
// purpose-built surface delegates here for what it has nothing better for.
[[nodiscard]] FieldEdit DrawDataField(JsonValue& value, const DataFieldSchema& field, const std::string& path,
                                      DataFormHost& host);

// Tooltip on hover, and SelectField on click. Call right after the widget.
void DrawDataFieldHelp(DataFormHost& host, const DataFieldSchema& field, std::string_view path);
