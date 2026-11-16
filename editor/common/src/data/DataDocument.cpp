#include "data/DataDocument.h"

#include <assets/data/DataDefinitionStaging.h>

#include "identity/RandomKey.h"

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
        ++Document.Committed;
    }

    void Undo() override
    {
        Document.ApplyRoot(Before);
        ++Document.Committed;
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
    document->Baseline.Record(file);
    document->Validate(types, schemas);
    return document;
}

std::unique_ptr<DataDocument> DataDocument::Create(
    const std::filesystem::path& file,
    std::string virtualPath,
    const DataAssetTypeRegistration& type,
    const DataSchema& schema,
    const JsonValue* data)
{
    JsonValue::Object envelope;
    envelope.emplace_back("type", JsonValue(type.Name));
    envelope.emplace_back("version", JsonValue(static_cast<double>(type.CurrentVersion)));
    envelope.emplace_back("data", data != nullptr ? *data : CreateDefaultDataValue(schema.Root));

    auto document = std::unique_ptr<DataDocument>(
        new DataDocument(file, std::move(virtualPath), JsonValue(std::move(envelope))));
    document->SavedText.clear();
    document->RefreshDirty();
    document->Baseline.RecordAbsent();
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
    return Baseline.FileChanged(File);
}

bool DataDocument::Save(std::string* error)
{
    CommitEdit();
    if (IsExternallyModified())
    {
        if (error)
            *error = "the file changed on disk since it was read";
        return false;
    }
    return Write(error);
}

bool DataDocument::SaveOverFile(std::string* error)
{
    CommitEdit();
    return Write(error);
}

bool DataDocument::Write(std::string* error)
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
    // Closing publishes buffered bytes and the final write timestamp. Sampling
    // it while the stream is open mistakes our own save for an external edit.
    output.close();
    if (!output.good())
    {
        if (error)
            *error = "failed while writing data asset";
        return false;
    }

    SavedText = formatted;
    RefreshDirty();
    Baseline.Record(File);
    return true;
}

std::optional<JsonValue> DataDocument::ReadFileRoot(std::string* error) const
{
    std::ifstream input(File);
    if (!input.is_open())
    {
        if (error)
            *error = "could not reopen data asset";
        return std::nullopt;
    }

    const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    JsonParseError parseError;
    std::optional<JsonValue> root = JsonParse(text, &parseError);
    if (!root)
    {
        if (error)
            *error = "JSON parse error at " + std::to_string(parseError.Position) + ": " + parseError.Message;
        return std::nullopt;
    }
    return root;
}

bool DataDocument::AdoptFileVersion(const DataAssetTypeRegistry& types,
                                    const DataSchemaRegistry& schemas,
                                    std::string* error)
{
    std::optional<JsonValue> root = ReadFileRoot(error);
    if (!root)
        return false;
    CancelEdit();
    SavedText = JsonFormat(*root);
    ReplaceRoot(std::move(*root));
    RefreshDirty();
    Baseline.Record(File);
    Validate(types, schemas);
    return true;
}

bool DataDocument::Reload(const DataAssetTypeRegistry& types,
                          const DataSchemaRegistry& schemas,
                          std::string* error)
{
    std::optional<JsonValue> root = ReadFileRoot(error);
    if (!root)
        return false;

    // Clear drops a pending edit's callback without running it, so the
    // transaction is abandoned here rather than cancelled: the state it guarded
    // is being replaced wholesale by the file's contents.
    Editing = false;
    EditBaseline = JsonValue();
    History.Clear();

    WorkingRoot = std::move(*root);
    ++ContentRevision;
    ++Committed;
    SavedText = JsonFormat(WorkingRoot);
    RefreshEnvelopeIdentity();
    RefreshDirty();
    Baseline.Record(File);
    Validate(types, schemas);
    return true;
}

void DataDocument::Validate(const DataAssetTypeRegistry& types,
                            const DataSchemaRegistry& schemas)
{
    // Checking a document asks what shape its references have, not where their
    // assets now are.
    const AssetPathsById current;
    DataSourceScope sources(Roots);
    DataDefinitionStaging staged = StageDataDefinition(
        WorkingRoot, DataDefinitionContext{ .Types = types, .Schemas = &schemas, .Current = current,
                                            .Read = sources.Reader(), .Asset = AssetPath, .Provenance = true });
    Errors = std::move(staged.Errors);
    Composed = std::move(staged.Data);
    Sources = std::move(staged.Sources);
    Composition.reset();
    if (staged.Composed)
        Composition = std::move(staged.Composition);
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

void RemintStableKeys(JsonValue& value, const DataFieldSchema& field)
{
    switch (field.Kind)
    {
    case DataFieldKind::StableKey:
        value = JsonValue(MintStableKey());
        return;
    case DataFieldKind::Record:
        if (!value.IsObject())
            return;
        for (const DataFieldSchema& child : field.Children)
            if (JsonValue* member = value.Find(child.Key))
                RemintStableKeys(*member, child);
        return;
    case DataFieldKind::Array:
        if (!value.IsArray() || field.Children.size() != 1)
            return;
        for (JsonValue& element : value.AsArray())
            RemintStableKeys(element, field.Children.front());
        return;
    case DataFieldKind::Optional:
        if (!value.IsNull() && field.Children.size() == 1)
            RemintStableKeys(value, field.Children.front());
        return;
    default:
        return;
    }
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
    // An entity reference starts unset. There is no identity this could invent
    // that would name an entity the author meant, and the empty string fails
    // validation loudly, which is the right thing for a reference nobody has
    // chosen yet.
    case DataFieldKind::Entity:
        return JsonValue("");
    case DataFieldKind::StableKey:
        return JsonValue(MintStableKey());
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
