#pragma once

#include "project/Project.h"
#include "properties/PropertySource.h"

#include <functional>
#include <optional>
#include <string>

//=============================================================================
// ProjectSettings
//
// One project's settings as properties: its name, its game module and its
// content roots. Every change is saved at once through the save it is given;
// a save that fails keeps what the file holds and says why.
//=============================================================================
class ProjectSettings final : public CommitOnlyPropertySource
{
public:
    using SaveFn = std::function<bool(const ProjectDescriptor&, std::string& error)>;

    explicit ProjectSettings(SaveFn save) : Save(std::move(save)) {}

    // What the chosen project holds, or why it could not be read.
    void Show(std::optional<ProjectDescriptor> descriptor, std::string problem = {});
    [[nodiscard]] const std::optional<ProjectDescriptor>& Shown() const { return Descriptor; }

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    bool BeginEdit(std::string_view key) override;
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;
    PropertyEditResult Act(std::string_view key, std::string_view verb, std::string_view choice) override;

private:
    PropertyEditResult Keep(ProjectDescriptor changed);

    SaveFn Save;
    std::optional<ProjectDescriptor> Descriptor;
    std::string Problem;
};
