#pragma once

#include <core/assets/AssetRef.h>
#include <core/json/JsonValue.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

enum class DataFieldKind : uint8_t
{
    Bool,
    Int,
    Float,
    String,
    Enum,
    Vector,
    Record,
    Array,
    Optional,
    AssetRef,
    DataAssetRef,
    GameplayTag,
};

using DataDefaultValue = std::variant<std::monostate, bool, int64_t, double, std::string>;

struct DataNumericConstraints
{
    std::optional<double> Minimum;
    std::optional<double> Maximum;
    std::optional<double> Step;
};

struct DataEnumChoice
{
    std::string Value;
    std::string DisplayName;
    std::string Description;
};

struct DataReferenceConstraints
{
    AssetType AssetTypeFilter = AssetType::Unknown;
    std::string DataSubtype;
};

// How an authoring surface should present a field, when the default layout
// reads badly for the data. Presentation only: a hint never changes what is
// authored or how it validates, and a surface that ignores hints is still
// correct.
//
// Recognized Widget values:
//   "cards"   on an Array  -- one collapsed row per element, titled by TitleKey,
//                             for arrays whose elements are records worth
//                             reading as a list rather than expanding at once.
//   "compact" on a Record  -- absent optional members are offered through one
//                             popup instead of a button each, for records with
//                             many optional members.
struct DataEditorHint
{
    std::string Widget;

    // Array element key whose value titles the row. Names the element by what
    // the author called it rather than by its index. Falls back to the element's
    // display name and position when absent or empty.
    std::string TitleKey;

    bool Multiline = false;
};

struct DataFieldSchema
{
    std::string Key;
    std::string DisplayName;
    std::string Summary;
    std::string Description;
    std::string Units;

    DataFieldKind Kind = DataFieldKind::String;
    DataDefaultValue Default;
    DataNumericConstraints Numeric;
    DataReferenceConstraints Reference;
    DataEditorHint Editor;

    // Record: one child per named member.
    // Array/Optional: exactly one child describing the element/value.
    std::vector<DataFieldSchema> Children;
    std::vector<DataEnumChoice> EnumChoices;
    uint32_t VectorLength = 3;

    bool Required = true;
    bool Advanced = false;
    bool ReadOnly = false;
    bool Deprecated = false;
};

struct DataSchema
{
    std::string TypeName;
    std::string DisplayName;
    std::string Description;
    DataFieldSchema Root;
    bool AllowUnknownFields = false;
};

struct DataValidationError
{
    std::string Path;
    std::string Message;
};

// The named member of a record schema, or null. Record members are a short
// ordered list, so this is a linear scan by design.
[[nodiscard]] const DataFieldSchema* FindChild(const DataFieldSchema& parent,
                                               std::string_view key);

[[nodiscard]] bool ValidateDataAgainstSchema(const JsonValue& value,
                                             const DataSchema& schema,
                                             std::vector<DataValidationError>& errors);

class DataSchemaRegistry
{
public:
    [[nodiscard]] bool Register(DataSchema schema);
    [[nodiscard]] bool Unregister(std::string_view typeName);

    [[nodiscard]] const DataSchema* Find(std::string_view typeName) const;
    [[nodiscard]] std::span<const DataSchema> Entries() const;

private:
    std::vector<DataSchema> Schemas;
};
