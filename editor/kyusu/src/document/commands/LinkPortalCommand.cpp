#include "LinkPortalCommand.h"

#include "RawComponentEditCommand.h"

#include "document/EditorDocument.h"

#include <cstring>
#include <utility>
#include <vector>

std::unique_ptr<ICommand> MakeLinkPortalCommand(EditorDocument& document, EntityId entity,
                                                TransitionId transition)
{
    EditorScene& scene = document.GetScene();
    const PortalComponent* portal = scene.TryGetPortal(entity);
    if (portal == nullptr)
        return nullptr;

    std::vector<std::byte> before(sizeof(PortalComponent));
    std::memcpy(before.data(), portal, sizeof(PortalComponent));
    const PortalComponent linked{ transition };
    std::vector<std::byte> after(sizeof(PortalComponent));
    std::memcpy(after.data(), &linked, sizeof(PortalComponent));

    const ComponentId component =
        scene.GetRegistry().Components.GetComponentId<PortalComponent>();
    return std::make_unique<RawComponentEditCommand>(entity, component, std::move(before),
                                                     std::move(after), scene, document);
}
