#include "DataSourceText.h"

#include "data/DataDocumentSet.h"
#include "properties/SettingProperties.h"

#include <core/json/JsonFormat.h>
#include <core/json/JsonParser.h>

#include <format>

std::vector<PropertyNode> DataSourceText::Nodes() const
{
    const DataDocument* document = Shown ? Shown() : nullptr;
    if (document == nullptr)
        return {};
    PropertyNode source = TextProperty("source", "Source", JsonFormat(document->Root()));
    source.Descriptor.Multiline = true;
    std::vector<PropertyNode> nodes{ std::move(source) };
    if (!Problem.empty())
        nodes.push_back(NoteProperty("problem", "Not applied", Problem));
    return nodes;
}

bool DataSourceText::BeginEdit(std::string_view key)
{
    return key == "source" && Shown && Shown() != nullptr;
}

PropertyEditResult DataSourceText::CommitEdit(std::string_view key, std::string_view value)
{
    DataDocument* document = Shown ? Shown() : nullptr;
    if (key != "source" || document == nullptr)
        return PropertyEditResult::Gone;
    JsonParseError error;
    std::optional<JsonValue> parsed = JsonParse(value, &error);
    if (!parsed.has_value())
    {
        Problem = std::format("It does not parse at {}: {}", error.Position, error.Message);
        return PropertyEditResult::Refused;
    }
    Problem.clear();
    document->ReplaceRoot(std::move(*parsed));
    Documents.Store().Changed(*document);
    return PropertyEditResult::Applied;
}
