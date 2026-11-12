#include "workspace/ScenePresentationSource.h"

#include "authoring/PresentationProperties.h"
#include "document/commands/ValueCommand.h"
#include "workspace/EditorWorkspace.h"

#include <render/GamePresentationLayer.h>

std::vector<PropertyNode> ScenePresentationSource::Nodes() const
{
    const PresentationSettingsLayer* game =
        Workspace.GamePresentation != nullptr ? Workspace.GamePresentation->Get() : nullptr;
    const PresentationLayerStack below = PresentationLayersAt(game, nullptr, {}, nullptr);
    return { PresentationProperties(Workspace.World.FocusDocument().GetPresentation(), below.Layers()) };
}

PropertyEditResult ScenePresentationSource::CommitEdit(std::string_view key, std::string_view value)
{
    if (!IsPresentationKey(key))
        return PropertyEditResult::Gone;
    EditorDocument& document = Workspace.World.FocusDocument();
    const std::optional<PresentationSettingsLayer> edited = EditPresentationLayer(document.GetPresentation(), key, value);
    if (!edited.has_value())
        return PropertyEditResult::Refused;
    if (*edited == document.GetPresentation())
        return PropertyEditResult::Unchanged;
    Workspace.Commands.Execute(MakeSetPresentationCommand(document.GetPresentation(), *edited, document));
    return PropertyEditResult::Applied;
}
