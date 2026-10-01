#pragma once

#include "properties/PropertySource.h"

#include <functional>
#include <string>
#include <vector>

// A new data asset as properties: what kind it is and where it goes, relative
// to the content root. Its one verb creates it through the creation it is
// given, which says why when it cannot.
class DataCreation final : public IPropertySource
{
public:
    using CreateFn = std::function<bool(const std::string& subtype, const std::string& path, std::string& error)>;

    DataCreation(std::function<std::vector<std::string>()> subtypes, CreateFn create)
        : Subtypes(std::move(subtypes))
        , Create(std::move(create))
    {
    }

    void Clear();

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    bool BeginEdit(std::string_view key) override;
    PropertyEditResult PreviewEdit(std::string_view, std::string_view) override { return PropertyEditResult::Unchanged; }
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;
    void CancelEdit(std::string_view) override {}
    PropertyEditResult Act(std::string_view key, std::string_view verb, std::string_view choice) override;

private:
    std::function<std::vector<std::string>()> Subtypes;
    CreateFn Create;
    std::string Subtype;
    std::string Path;
    std::string Problem;
};
