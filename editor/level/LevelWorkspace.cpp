#include "LevelWorkspace.h"

#include "editmodes/BrushEditSession.h"
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
        Preview);

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

    SelectionSubscription = Selection.Subscribe([this](SelectableRef ref)
    {
        if (ref.IsValid() && Document.GetScene().TryGetBrush(ref.Entity) != nullptr)
            Sessions.SetSession(std::make_unique<BrushEditSession>(ref, Document.GetScene(), Document));
        else
            Sessions.EndSession();
    });
}
