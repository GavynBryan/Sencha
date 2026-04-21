#pragma once

#include "../commands/CommandStack.h"
#include "../editmodes/EditSessionHost.h"
#include "../input/ViewportToolDispatcher.h"
#include "../interaction/InteractionHost.h"
#include "../render/PreviewBuffer.h"
#include "../selection/SelectionContext.h"
#include "../selection/SelectionService.h"
#include "../tools/ToolContext.h"
#include "../tools/ToolRegistry.h"
#include "../viewport/FourWayViewportLayout.h"
#include "../viewport/Picking.h"

#include "LevelDocument.h"

#include <functional>
#include <memory>

class LevelWorkspace
{
public:
    LevelWorkspace();

    void Init(CommandStack& commands);

    LevelDocument Document;
    FourWayViewportLayout Layout;
    SelectionContext LevelSelection;
    SelectionService Selection;
    PickingService Picking;
    InteractionHost Interactions;
    PreviewBuffer Preview;
    EditSessionHost Sessions;
    std::shared_ptr<SelectionService::ObserverFn> SelectionSubscription;
    std::unique_ptr<ToolContext> ActiveToolContext;
    std::unique_ptr<ToolRegistry> Tools;
    std::unique_ptr<ViewportToolDispatcher> Dispatcher;
};
