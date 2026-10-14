#pragma once

#include "data/DataDocumentSet.h"
#include "properties/PropertySource.h"

#include <core/json/JsonValue.h>

#include <functional>
#include <string>
#include <vector>

class DataDocument;
class DataSubtypeAuthoringRegistry;
class IDataSubtypeAuthoring;
struct DataFieldSchema;

// A data document's "data" object as properties, laid out by its authoring
// schema and keyed by JSON path ("$.data.layers[1].speed"); each edit runs in
// the document's own transaction (editor/ARCHITECTURE.md, Data workspace).
class DataProperties final : public IPropertySource
{
public:
    // `document` names the document shown, or null for none; `subtypes`, when
    // given, add to the schema's properties for their documents.
    DataProperties(DataDocumentSet& documents, std::function<DataDocument*()> document,
                   const DataSubtypeAuthoringRegistry* subtypes = nullptr)
        : Documents(documents)
        , Shown(std::move(document))
        , Subtypes(subtypes)
    {
    }

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    bool BeginEdit(std::string_view key) override;
    PropertyEditResult PreviewEdit(std::string_view key, std::string_view value) override;
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;
    void CancelEdit(std::string_view key) override;
    [[nodiscard]] std::vector<std::string> Candidates(std::string_view key) const override;
    PropertyEditResult Act(std::string_view key, std::string_view verb, std::string_view choice) override;

private:
    // The document's root with `change` applied to the value at `key`.
    PropertyEditResult Change(std::string_view key,
                              const std::function<bool(JsonValue& value, const DataFieldSchema&)>& change, DataEdit edit);
    PropertyEditResult Replace(JsonValue data, DataEdit edit);

    [[nodiscard]] IDataSubtypeAuthoring* AuthoringOf(const DataDocument& document) const;

    DataDocumentSet& Documents;
    std::function<DataDocument*()> Shown;
    const DataSubtypeAuthoringRegistry* Subtypes = nullptr;
};

// What a field is called wherever it is shown.
[[nodiscard]] std::string DataFieldDisplayName(const DataFieldSchema& field);
