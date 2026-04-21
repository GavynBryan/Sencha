#pragma once

#include "../commands/CommandStack.h"
#include "../selection/SelectionContext.h"
#include "../selection/SelectionService.h"
#include "../tools/ToolContext.h"
#include "../tools/ToolRegistry.h"
#include "../viewport/FourWayViewportLayout.h"
#include "../viewport/Picking.h"

#include "LevelDocument.h"

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
    std::unique_ptr<ToolContext> ActiveToolContext;
    std::unique_ptr<ToolRegistry> Tools;
};
