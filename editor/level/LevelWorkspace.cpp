#include "LevelWorkspace.h"

#include "editmodes/MeshEditSession.h"
#include "tools/CameraTool.h"
#include "tools/BrushTool.h"
#include "tools/SelectTool.h"

#include <memory>

LevelWorkspace::LevelWorkspace()
    : Selection(LevelSelection)
{
}

void LevelWorkspace::Init(CommandStack& commands)
{
    ActiveToolContext = std::make_unique<ToolContext>(
        commands,
        Selection,
        Picking,
        Document.GetScene(),
        Document,
        Interactions,
        Preview,
        MeshEdit);

    Tools = std::make_unique<ToolRegistry>(*ActiveToolContext);
    Tools->Register(std::make_unique<SelectTool>());
    Tools->Register(std::make_unique<BrushTool>());
    Tools->Register(std::make_unique<CameraTool>());
    Tools->Activate("select");

    Dispatcher = std::make_unique<ViewportToolDispatcher>(
        Layout,
        *ActiveToolContext,
        Interactions,
        Sessions,
        *Tools);

    // The mesh-edit session reads selection and element mode live from the tool
    // context on each pointer-down, so it never needs rebuilding when the
    // selection or mode changes. It consumes a click only when a gizmo axis is
    // hit; otherwise the select tool runs and picks normally.
    Sessions.SetSession(std::make_unique<MeshEditSession>());
}
