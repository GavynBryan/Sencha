#include "DataDocument.h"

#include "commands/ICommand.h"

#include <core/json/JsonFormat.h>
#include <core/json/JsonParser.h>

#include <cmath>
#include <fstream>
#include <iterator>
#include <optional>
#include <utility>

namespace
{
    JsonValue DefaultFromVariant(const DataDefaultValue& value)
    {
        return std::visit([](const auto& item) -> JsonValue
        {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, std::monostate>)
                return JsonValue();
            else if constexpr (std::is_same_v<T, int64_t>)
                return JsonValue(static_cast<double>(item));
            else
                return JsonValue(item);
        }, value);
    }

    bool HasExplicitDefault(const DataDefaultValue& value)
    {
        return !std::holds_alternative<std::monostate>(value);
    }

    void AddValidation(std::vector<DataValidationError>& errors,
                       std::string path,
                       std::string message)
    {
        errors.push_back(DataValidationError{
            .Path = std::move(path),
            .Message = std::move(message),
        });
    }
}

class DataDocument::ReplaceRootCommand final : public ICommand
{
public:
    ReplaceRootCommand(DataDocument& document, JsonValue before, JsonValue after)
        : Document(document)
        , Before(std::move(before))
        , After(std::move(after))
    {
    }

    void Execute() override
    {
        Document.ApplyRoot(After);
    }

    void Undo() override
    {
        Document.ApplyRoot(Before);
    }

private:
    DataDocument& Document;
    JsonValue Before;
    JsonValue After;
};

std::unique_ptr<DataDocument> DataDocument::Open(
    const std::filesystem::path& file,
    std::string virtualPath,
    const DataAssetTypeRegistry& types,
    const DataSchemaRegistry& schemas,
    std::string* error)
{
    std::ifstream input(file);
    if (!input.is_open())
    {
        if (error)
            *error = "could not open data asset";
        return nullptr;
    }

    const std::string text{std::istreambuf_iterator<char>(input),
                           std::istreambuf_iterator<char>()};
    JsonParseError parseError;
    std::optional<JsonValue> root = JsonParse(text, &parseError);
    if (!root)
    {
        if (error)
            *error = "JSON parse error at " + std::to_string(parseError.Position)
                   + ": " + parseError.Message;
        return nullptr;
    }

    auto document = std::unique_ptr<DataDocument>(
        new DataDocument(file, std::move(virtualPath), std::move(*root)));
    document->SavedText = JsonFormat(document->WorkingRoot);
    document->RefreshDirty();
    document->RefreshTimestamp();
    document->Validate(types, schemas);
    return document;
}

std::unique_ptr<DataDocument> DataDocument::Create(
    const std::filesystem::path& file,
    std::string virtualPath,
    const DataAssetTypeRegistration& type,
    const DataSchema& schema)
{
    JsonValue::Object envelope;
    envelope.emplace_back("type", JsonValue(type.Name));
    envelope.emplace_back("version", JsonValue(static_cast<double>(type.CurrentVersion)));
    envelope.emplace_back("data", CreateDefaultDataValue(schema.Root));

    auto document = std::unique_ptr<DataDocument>(
        new DataDocument(file, std::move(virtualPath), JsonValue(std::move(envelope))));
    document->SavedText.clear();
    document->RefreshDirty();
    return document;
}

DataDocument::DataDocument(std::filesystem::path file,
                           std::string virtualPath,
                           JsonValue root)
    : File(std::move(file))
    , AssetPath(std::move(virtualPath))
    , WorkingRoot(std::move(root))
{
    RefreshEnvelopeIdentity();
}

void DataDocument::ReplaceRoot(JsonValue root)
{
    if (JsonFormat(root) == JsonFormat(WorkingRoot))
        return;

    History.Execute(std::make_unique<ReplaceRootCommand>(
        *this, WorkingRoot, std::move(root)));
}

void DataDocument::BeginEdit()
{
    if (Editing)
        return;

    Editing = true;
    EditBaseline = WorkingRoot;

    // Undo during an interaction reverts the interaction itself before reaching
    // committed history. The callback is idempotent through Editing because the
    // stack can invoke it, and so can a later Begin claiming the scope.
    History.OpenPendingEdit([this]
    {
        if (!Editing)
            return;
        Editing = false;
        ApplyRoot(EditBaseline);
    });
}

void DataDocument::PreviewRoot(JsonValue root)
{
    ApplyRoot(std::move(root));
}

void DataDocument::CommitEdit()
{
    if (!Editing)
        return;

    Editing = false;
    History.ClosePendingEdit();

    // An interaction that ended where it started leaves no history behind.
    if (JsonFormat(WorkingRoot) == JsonFormat(EditBaseline))
        return;

    History.Execute(std::make_unique<ReplaceRootCommand>(
        *this, std::move(EditBaseline), WorkingRoot));
    EditBaseline = JsonValue();
}

void DataDocument::CancelEdit()
{
    if (!Editing)
        return;

    Editing = false;
    History.ClosePendingEdit();
    ApplyRoot(EditBaseline);
    EditBaseline = JsonValue();
}

void DataDocument::Undo()
{
    History.Undo();
}

void DataDocument::Redo()
{
    History.Redo();
}

bool DataDocument::IsExternallyModified() const
{
    if (!HasWriteTime)
        return false;

    std::error_code error;
    const auto current = std::filesystem::last_write_time(File, error);
    return !error && current != LastWriteTime;
}

bool DataDocument::Save(std::string* error)
{
    std::error_code directoryError;
    if (!File.parent_path().empty())
        std::filesystem::create_directories(File.parent_path(), directoryError);
    if (directoryError)
    {
        if (error)
            *error = directoryError.message();
        return false;
    }

    const std::string formatted = JsonFormat(WorkingRoot);
    std::ofstream output(File, std::ios::trunc);
    if (!output.is_open())
    {
        if (error)
            *error = "could not open data asset for writing";
        return false;
    }
    output << formatted;
    if (!output.good())
    {
        if (error)
            *error = "failed while writing data asset";
        return false;
    }

    SavedText = formatted;
    RefreshDirty();
    RefreshTimestamp();
    return true;
}

bool DataDocument::Reload(const DataAssetTypeRegistry& types,
                          const DataSchemaRegistry& schemas,
                          std::string* error)
{
    std::ifstream input(File);
    if (!input.is_open())
    {
        if (error)
            *error = "could not reopen data asset";
        return false;
    }

    const std::string text{std::istreambuf_iterator<char>(input),
                           std::istreambuf_iterator<char>()};
    JsonParseError parseError;
    std::optional<JsonValue> root = JsonParse(text, &parseError);
    if (!root)
    {
        if (error)
            *error = "JSON parse error at " + std::to_string(parseError.Position)
                   + ": " + parseError.Message;
        return false;
    }

    // Clear drops a pending edit's callback without running it, so the
    // transaction is abandoned here rather than cancelled: the state it guarded
    // is being replaced wholesale by the file's contents.
    Editing = false;
    EditBaseline = JsonValue();
    History.Clear();

    WorkingRoot = std::move(*root);
    ++ContentRevision;
    SavedText = JsonFormat(WorkingRoot);
    RefreshEnvelopeIdentity();
    RefreshDirty();
    RefreshTimestamp();
    Validate(types, schemas);
    return true;
}

void DataDocument::Validate(const DataAssetTypeRegistry& types,
                            const DataSchemaRegistry& schemas)
{
    Errors.clear();
    if (!WorkingRoot.IsObject())
    {
        AddValidation(Errors, "$", "data asset root must be an object");
        return;
    }

    const JsonValue* typeValue = WorkingRoot.Find("type");
    const JsonValue* versionValue = WorkingRoot.Find("version");
    const JsonValue* dataValue = WorkingRoot.Find("data");
    if (typeValue == nullptr || !typeValue->IsString())
        AddValidation(Errors, "$.type", "required string field is missing");
    if (versionValue == nullptr || !versionValue->IsNumber()
        || versionValue->AsNumber() < 1.0
        || std::floor(versionValue->AsNumber()) != versionValue->AsNumber())
    {
        AddValidation(Errors, "$.version", "required positive integer field is missing");
    }
    if (dataValue == nullptr)
        AddValidation(Errors, "$.data", "required data field is missing");
    if (!Errors.empty())
        return;

    const std::string& typeName = typeValue->AsString();
    const DataAssetTypeRegistration* type = types.Find(typeName);
    if (type == nullptr)
    {
        AddValidation(Errors, "$.type", "unknown data asset subtype");
        return;
    }

    const uint32_t version = static_cast<uint32_t>(versionValue->AsNumber());
    if (version != type->CurrentVersion)
    {
        AddValidation(Errors, "$.version",
                      "file version does not match the registered subtype version");
    }

    if (const DataSchema* schema = schemas.Find(typeName))
    {
        std::vector<DataValidationError> schemaErrors;
        (void)ValidateDataAgainstSchema(*dataValue, *schema, schemaErrors);
        for (DataValidationError& schemaError : schemaErrors)
        {
            if (schemaError.Path == "$")
                schemaError.Path = "$.data";
            else if (schemaError.Path.starts_with("$."))
                schemaError.Path.insert(1, ".data");
            Errors.push_back(std::move(schemaError));
        }
    }

    // Compiled even when the schema already complained: the two catch different
    // mistakes, and reporting only the first found means fixing a missing field
    // reveals a binding error that was there all along.
    DataAssetCompileResult compiled = type->Compile(*dataValue);
    if (!compiled.IsValid())
    {
        Errors.push_back(compiled.Error.empty()
            ? DataValidationError{ "$.data", "subtype compiler returned no value" }
            : SplitCompileError(std::move(compiled.Error)));
    }
}

const DataValidationError* FindValidationErrorAt(
    std::span<const DataValidationError> errors, std::string_view path)
{
    for (const DataValidationError& error : errors)
    {
        if (error.Path == path)
            return &error;
        if (error.Path.size() > path.size()
            && error.Path.compare(0, path.size(), path) == 0
            && (error.Path[path.size()] == '.' || error.Path[path.size()] == '['))
        {
            return &error;
        }
    }
    return nullptr;
}

DataValidationError SplitCompileError(std::string message)
{
    // Subtype compilers format their errors as "<json-path> <what went wrong>".
    // Recovering the path lands the error on the field that owns it; collapsing
    // every one onto "$.data" put them all on the document root, where a form
    // cannot mark the offending card.
    if (!message.starts_with("$."))
        return DataValidationError{ "$.data", std::move(message) };

    const auto pathEnd = message.find_first_not_of(
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.$[]");
    if (pathEnd == std::string::npos)
        return DataValidationError{ std::move(message), std::string() };

    std::string path = message.substr(0, pathEnd);
    std::string rest = message.substr(pathEnd);
    if (const auto textStart = rest.find_first_not_of(" \t");
        textStart != std::string::npos)
    {
        rest.erase(0, textStart);
    }
    return DataValidationError{ std::move(path), std::move(rest) };
}

void DataDocument::ApplyRoot(JsonValue root)
{
    WorkingRoot = std::move(root);
    ++ContentRevision;
    RefreshEnvelopeIdentity();
    RefreshDirty();
}

void DataDocument::RefreshEnvelopeIdentity()
{
    TypeName.clear();
    FileVersion = 0;
    if (!WorkingRoot.IsObject())
        return;

    if (const JsonValue* type = WorkingRoot.Find("type"); type && type->IsString())
        TypeName = type->AsString();
    if (const JsonValue* version = WorkingRoot.Find("version"); version && version->IsNumber()
        && version->AsNumber() >= 0.0)
    {
        FileVersion = static_cast<uint32_t>(version->AsNumber());
    }
}

void DataDocument::RefreshDirty()
{
    Dirty = JsonFormat(WorkingRoot) != SavedText;
}

void DataDocument::RefreshTimestamp()
{
    std::error_code error;
    LastWriteTime = std::filesystem::last_write_time(File, error);
    HasWriteTime = !error;
}

JsonValue CreateDefaultDataValue(const DataFieldSchema& field)
{
    if (HasExplicitDefault(field.Default))
        return DefaultFromVariant(field.Default);

    switch (field.Kind)
    {
    case DataFieldKind::Bool:
        return JsonValue(false);
    case DataFieldKind::Int:
    case DataFieldKind::Float:
        return JsonValue(0.0);
    case DataFieldKind::String:
    case DataFieldKind::AssetRef:
    case DataFieldKind::DataAssetRef:
    case DataFieldKind::GameplayTag:
        return JsonValue("");
    case DataFieldKind::Enum:
        return field.EnumChoices.empty()
            ? JsonValue("")
            : JsonValue(field.EnumChoices.front().Value);
    case DataFieldKind::Vector:
    {
        JsonValue::Array values;
        values.resize(field.VectorLength, JsonValue(0.0));
        return JsonValue(std::move(values));
    }
    case DataFieldKind::Record:
    {
        JsonValue::Object object;
        // Only required members are materialized. An optional member's default
        // is the value it starts at when the author adds it, not a reason to
        // write it out unasked -- otherwise a record with many optional members
        // arrives fully populated and the few that were set are lost in it.
        for (const DataFieldSchema& child : field.Children)
        {
            if (child.Required)
                object.emplace_back(child.Key, CreateDefaultDataValue(child));
        }
        return JsonValue(std::move(object));
    }
    case DataFieldKind::Array:
        return JsonValue(JsonValue::Array{});
    case DataFieldKind::Optional:
        return JsonValue();
    }

    return JsonValue();
}
