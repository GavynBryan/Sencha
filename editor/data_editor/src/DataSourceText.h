#pragma once

#include "properties/PropertySource.h"

#include <functional>
#include <string>

class DataDocument;
class DataDocumentSet;

// A data document's JSON as one long property, for reading it as the file does
// and for the edits the form has no field for. A commit that parses replaces
// the document as one step; one that does not is refused, and says where.
class DataSourceText final : public CommitOnlyPropertySource
{
public:
    DataSourceText(DataDocumentSet& documents, std::function<DataDocument*()> document)
        : Documents(documents)
        , Shown(std::move(document))
    {
    }

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    bool BeginEdit(std::string_view key) override;
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;

private:
    DataDocumentSet& Documents;
    std::function<DataDocument*()> Shown;
    std::string Problem;
};
