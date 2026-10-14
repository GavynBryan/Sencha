#pragma once

#include "properties/PropertySource.h"

#include <functional>
#include <string>
#include <vector>

//=============================================================================
// ProjectCreation
//
// A new project as properties: where it goes, what it is called, and the
// starter template it copies, or none for the descriptor alone. Its one verb
// creates it through the creation it is given, which says why when it cannot.
//=============================================================================
class ProjectCreation final : public CommitOnlyPropertySource
{
public:
    // `templateName` is one of the templates offered, or empty for none.
    using CreateFn = std::function<bool(const std::string& directory, const std::string& name,
                                        const std::string& templateName, std::string& error)>;

    ProjectCreation(std::vector<std::string> templates, CreateFn create);

    // Starts a fresh form.
    void Clear();

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;
    PropertyEditResult Act(std::string_view key, std::string_view verb, std::string_view choice) override;

private:
    [[nodiscard]] std::vector<std::string_view> TemplateChoices() const;

    std::vector<std::string> Templates;
    CreateFn Create;
    std::string Directory;
    std::string Name;
    // Index into Templates, or Templates.size() for none.
    std::size_t Template = 0;
    std::string Problem;
};
