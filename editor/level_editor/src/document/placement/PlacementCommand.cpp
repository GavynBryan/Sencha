#include "PlacementCommand.h"

#include "PlacementSession.h"

#include "commands/CommandStack.h"
#include "document/EditorDocument.h"
#include "document/commands/PlaceEntityCommand.h"
#include "document/commands/SceneInstanceCommands.h"

#include <world/serialization/ComponentSerializerRegistry.h>

std::string PlacementRefusal(const PlacementSubject& subject, EditorDocument& document)
{
    if (subject.What == PlacementSubject::Kind::SceneSource)
    {
        std::string error;
        return document.CanPlaceSceneSource(subject.Key, &error) ? std::string{} : error;
    }
    if (subject.Key.empty())
        return {};
    const IComponentSerializer* serializer = EditorSceneSerializers().FindByJsonKey(subject.Key);
    if (serializer == nullptr || serializer->Placement().Label.empty())
        return "'" + subject.Key + "' is not a component that places on its own";
    return {};
}

std::unique_ptr<ICommand> MakePlacementCommand(const PendingPlacement& pending, EditorDocument& document,
                                               SelectionService& selection)
{
    if (!PlacementRefusal(pending.Subject, document).empty())
        return nullptr;
    if (pending.Subject.What == PlacementSubject::Kind::SceneSource)
        return std::make_unique<PlaceSceneInstanceCommand>(pending.Subject.Key, pending.Placement, document,
                                                           selection);
    return std::make_unique<PlaceEntityCommand>(pending.Subject.Key, pending.Placement, document, selection);
}

std::optional<PendingPlacement> PlacePending(PlacementSession& session, EditorDocument& document,
                                             SelectionService& selection, CommandStack& commands)
{
    std::optional<PendingPlacement> placed = session.Commit();
    if (!placed.has_value())
        return std::nullopt;
    std::unique_ptr<ICommand> command = MakePlacementCommand(*placed, document, selection);
    if (command == nullptr)
        return std::nullopt;
    commands.Execute(std::move(command));
    return placed;
}
