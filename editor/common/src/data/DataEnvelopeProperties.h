#pragma once

#include "data/DataDocumentSet.h"
#include "properties/PropertySource.h"

#include <core/json/JsonValue.h>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

class DataDocument;

class DataEnvelopeProperties final : public IPropertySource
{
public:
    DataEnvelopeProperties(DataDocumentSet& documents, std::function<DataDocument*()> document)
        : Documents(documents)
        , Shown(std::move(document))
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
    PropertyEditResult Rewrite(const std::function<bool(JsonValue::Object& envelope)>& rewrite, DataEdit edit);
    [[nodiscard]] JsonValue Reference(std::string_view path) const;

    DataDocumentSet& Documents;
    std::function<DataDocument*()> Shown;
};
