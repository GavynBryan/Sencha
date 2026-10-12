#include "authoring/AnimationStageProperties.h"

#include "authoring/AnimationPreviewWorkspace.h"
#include "authoring/AnimationRigOutline.h"

#include "properties/SettingProperties.h"

#include <format>

namespace
{
std::string Chosen(const std::string& path, std::string_view none)
{
    return path.empty() ? std::string(none) : path;
}

std::string_view StateName(AnimationRigDependency::State state)
{
    switch (state)
    {
    case AnimationRigDependency::State::Resident:     return "loaded";
    case AnimationRigDependency::State::Missing:      return "missing";
    case AnimationRigDependency::State::WrongSubtype: return "not that kind of asset";
    case AnimationRigDependency::State::OtherKind:    return "";
    }
    return "";
}
} // namespace

std::vector<PropertyNode> AnimationAuditionProperties::Nodes() const
{
    const AnimationAuditionSelection& audition = Workspace.Audition;
    std::vector<PropertyNode> nodes{
        NoteProperty("mesh", "Mesh", Chosen(audition.MeshPath, "None")),
        NoteProperty("skeleton", "Skeleton", Chosen(audition.Session.SkeletonPath(), "None")),
        NoteProperty("clip", "Clip", Chosen(audition.ClipPath, "Bind pose")),
        NoteProperty("material", "Material", Chosen(audition.MaterialPath, "Neutral preview material")),
        NoteProperty("joints", "Joints", std::to_string(audition.Session.Skeleton().Joints.size())),
    };
    if (!audition.Error.empty())
        nodes.push_back(NoteProperty("refused", "Refused",
                                     audition.Error + " The previous content is still shown."));
    return nodes;
}

std::vector<PropertyNode> AnimationScenarioProperties::Nodes() const
{
    const AnimationPreviewSession& session = Workspace.Rig.Simulation;
    if (!session.IsOpen() || Workspace.Rig.Path.empty())
    {
        if (Workspace.Rig.Error.empty())
            return {};
        return { NoteProperty("error", "Rig refused", Workspace.Rig.Error) };
    }
    const AnimationScenario& scenario = session.Scenario();
    std::vector<PropertyNode> nodes{
        NoteProperty("name", "Scenario", scenario.Name + (Workspace.Rig.Scenario.IsChanged() ? ", unsaved" : "")),
        NoteProperty("file", "File", Workspace.Rig.Scenario.Path().generic_string()),
        NoteProperty("rate", "Rate", std::format("{} Hz, {} actions", scenario.TickRate, scenario.Actions.size())),
    };
    if (!Workspace.Rig.Error.empty())
        nodes.push_back(NoteProperty("error", "Problem", Workspace.Rig.Error));

    PropertyNode participants = GroupProperty("participants", "Participants");
    for (std::size_t i = 0; i < scenario.Participants.size(); ++i)
        participants.Children.push_back(
            NoteProperty(std::format("participants.{}", i), "Participant", scenario.Participants[i]));
    participants.Children.push_back(TextProperty("participants.add", "Add", {}, "A participant's name"));
    nodes.push_back(std::move(participants));

    PropertyNode tags = GroupProperty("tags", "Fixture tags");
    tags.Descriptor.Tooltip = "Names a game module would declare, declared by this scenario instead; they exist only "
                              "in the preview. Changing them replays to the current tick.";
    for (const std::string& tag : scenario.DeclaredTags)
    {
        PropertyNode node = NoteProperty("tags." + tag, "Tag", tag);
        node.Descriptor.Verbs = { { "remove", "Remove" } };
        tags.Children.push_back(std::move(node));
    }
    tags.Children.push_back(TextProperty("tags.declare", "Declare", {}, "A gameplay tag name"));
    if (!TagError.empty())
        tags.Children.push_back(NoteProperty("tags.error", "Refused", TagError));
    nodes.push_back(std::move(tags));

    PropertyNode dependencies = GroupProperty("dependencies", "Dependencies");
    dependencies.Descriptor.Advanced = true;
    std::size_t index = 0;
    for (const AnimationRigDependency& row : DescribeAnimationRigDependencies(Workspace.DataCache(), Workspace.Rig.Path))
    {
        const std::string_view state = StateName(row.Status);
        dependencies.Children.push_back(NoteProperty(
            std::format("dependencies.{}", index++), std::string(static_cast<std::size_t>(row.Depth) * 2, ' ') + row.Role,
            state.empty() ? row.Path : std::format("{}, {}", row.Path, state)));
    }
    nodes.push_back(std::move(dependencies));
    return nodes;
}

bool AnimationScenarioProperties::BeginEdit(std::string_view key)
{
    return key == "participants.add" || key == "tags.declare";
}

PropertyEditResult AnimationScenarioProperties::CommitEdit(std::string_view key, std::string_view value)
{
    if (value.empty())
        return PropertyEditResult::Unchanged;
    AnimationPreviewSession& session = Workspace.Rig.Simulation;
    if (key == "participants.add")
    {
        if (!session.AddParticipant(std::string(value)))
            return PropertyEditResult::Refused;
        session.Restart();
        return PropertyEditResult::Applied;
    }
    if (key == "tags.declare")
    {
        if (!session.DeclareTag(std::string(value), &TagError))
            return PropertyEditResult::Refused;
        TagError.clear();
        return PropertyEditResult::Applied;
    }
    return PropertyEditResult::Gone;
}

PropertyEditResult AnimationScenarioProperties::Act(std::string_view key, std::string_view verb, std::string_view)
{
    if (verb != "remove" || !key.starts_with("tags."))
        return PropertyEditResult::Gone;
    return Workspace.Rig.Simulation.UndeclareTag(std::string(key.substr(5))) ? PropertyEditResult::Applied
                                                                              : PropertyEditResult::Refused;
}
