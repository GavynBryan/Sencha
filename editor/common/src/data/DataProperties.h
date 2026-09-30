#pragma once

#include "properties/PropertySource.h"

#include <core/json/JsonValue.h>

#include <functional>
#include <string>
#include <vector>

class DataDocument;
class DataDocumentSet;
class DataSubtypeAuthoringRegistry;
class IDataSubtypeAuthoring;
struct DataFieldSchema;

//=============================================================================
// DataProperties
//
// A data document's "data" object as properties, laid out by its authoring
// schema: a record's members, an array's elements with their verbs (add,
// duplicate, delete, move), an optional member switched on and off, a
// reference to another data asset chosen among those of its subtype. Each
// property is keyed by its JSON path ("$.data.layers[1].speed"), and a
// validation error at a field is a note beside it. Every edit runs in the
// document's own transaction, so a drag is one step and every interruption
// cancels it.
//=============================================================================
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
    // The document's root with `change` applied to the value at `key`, as one
    // step; false when the change is refused.
    PropertyEditResult Change(std::string_view key, const std::function<bool(JsonValue& value, const DataFieldSchema&)>& change,
                              bool step);

    [[nodiscard]] IDataSubtypeAuthoring* AuthoringOf(const DataDocument& document) const;

    DataDocumentSet& Documents;
    std::function<DataDocument*()> Shown;
    const DataSubtypeAuthoringRegistry* Subtypes = nullptr;
};
