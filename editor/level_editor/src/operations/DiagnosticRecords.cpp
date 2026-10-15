#include "DiagnosticRecords.h"

#include <core/metadata/DataSchemaFields.h>

#include <format>
#include <string_view>
#include <vector>

namespace
{
    std::string_view KindText(ContentRiskSourceKind kind)
    {
        switch (kind)
        {
        case ContentRiskSourceKind::World:
            return "world";
        case ContentRiskSourceKind::Graph:
            return "graph";
        case ContentRiskSourceKind::Zone:
            return "zone";
        case ContentRiskSourceKind::Dock:
            return "dock";
        case ContentRiskSourceKind::Link:
            return "link";
        case ContentRiskSourceKind::Transition:
            return "transition";
        case ContentRiskSourceKind::Entity:
            return "entity";
        }
        return "world";
    }

    DataFieldSchema Text(std::string key, std::string summary = {})
    {
        return DataField::Field(std::move(key), DataFieldKind::String, {}, std::move(summary));
    }
}

DataFieldSchema ContentRiskRecordsSchema(std::string key)
{
    std::vector<DataEnumChoice> kinds;
    for (std::string_view kind : { "world", "graph", "zone", "dock", "link", "transition", "entity" })
        kinds.push_back({ std::string(kind), std::string(kind), {} });
    return DataField::ArrayOf(std::move(key), {}, {},
        DataField::Record({}, {}, {}, {
            DataField::Enum("severity", {}, {}, { { "warning", "Warning", {} }, { "error", "Error", {} } }),
            Text("rule"),
            DataField::Enum("kind", {}, "what the record is about", std::move(kinds)),
            Text("source", "the id of what it is about, in hex; empty for the world"),
            Text("message"),
        }),
        true);
}

JsonValue ContentRiskRecordsJson(std::span<const ContentRiskRecord> records)
{
    JsonValue::Array array;
    for (const ContentRiskRecord& record : records)
        array.push_back(JsonValue(JsonValue::Object{
            { "severity", JsonValue(record.Severity == ContentRiskSeverity::Error ? "error" : "warning") },
            { "rule", JsonValue(record.RuleId) },
            { "kind", JsonValue(std::string(KindText(record.Kind))) },
            { "source", JsonValue(record.SourceId == 0 ? std::string{} : std::format("{:016x}", record.SourceId)) },
            { "message", JsonValue(record.Message) },
        }));
    return JsonValue(std::move(array));
}
