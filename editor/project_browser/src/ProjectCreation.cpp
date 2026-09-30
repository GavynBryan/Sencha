#include "ProjectCreation.h"

#include "properties/SettingProperties.h"

namespace
{
constexpr std::string_view kNoTemplate = "None";
}

ProjectCreation::ProjectCreation(std::vector<std::string> templates, CreateFn create)
    : Templates(std::move(templates))
    , Create(std::move(create))
{
}

void ProjectCreation::Clear()
{
    Directory.clear();
    Name.clear();
    Template = 0;
    Problem.clear();
}

std::vector<std::string_view> ProjectCreation::TemplateChoices() const
{
    std::vector<std::string_view> choices(Templates.begin(), Templates.end());
    choices.push_back(kNoTemplate);
    return choices;
}

std::vector<PropertyNode> ProjectCreation::Nodes() const
{
    PropertyNode project = GroupProperty("project", "Project");
    project.Descriptor.Verbs.push_back({ .Id = "create", .Label = "Create" });
    project.Children.push_back(TextProperty("directory", "Directory", Directory, "Where the project goes"));
    project.Children.push_back(TextProperty("name", "Name", Name, "Left empty, the directory's name"));
    const std::vector<std::string_view> choices = TemplateChoices();
    project.Children.push_back(ChoiceProperty("template", "Template", choices, Template,
                                              Template < Templates.size()
                                                  ? "Copies the template's sources and content, ready to build"
                                                  : "Only the project file and an assets folder"));
    if (!Problem.empty())
        project.Children.push_back(NoteProperty("problem", "Not created", Problem));
    return { std::move(project) };
}

bool ProjectCreation::BeginEdit(std::string_view key)
{
    return key == "directory" || key == "name" || key == "template";
}

PropertyEditResult ProjectCreation::CommitEdit(std::string_view key, std::string_view value)
{
    if (key == "directory")
        Directory = std::string(value);
    else if (key == "name")
        Name = std::string(value);
    else if (key == "template")
    {
        const std::optional<std::size_t> chosen = ParseChoice(value, TemplateChoices());
        if (!chosen.has_value())
            return PropertyEditResult::Refused;
        Template = *chosen;
    }
    else
        return PropertyEditResult::Gone;
    return PropertyEditResult::Applied;
}

PropertyEditResult ProjectCreation::Act(std::string_view key, std::string_view verb, std::string_view)
{
    if (key != "project" || verb != "create")
        return PropertyEditResult::Gone;
    if (Directory.empty())
    {
        Problem = "Say where the project goes";
        return PropertyEditResult::Refused;
    }
    std::string error;
    const std::string templateName = Template < Templates.size() ? Templates[Template] : std::string{};
    if (!Create || !Create(Directory, Name, templateName, error))
    {
        Problem = error.empty() ? std::string("The project could not be created") : error;
        return PropertyEditResult::Refused;
    }
    Clear();
    return PropertyEditResult::Applied;
}
