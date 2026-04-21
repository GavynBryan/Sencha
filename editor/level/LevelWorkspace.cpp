#include "LevelWorkspace.h"

#include "tools/CameraTool.h"
#include "tools/CubeTool.h"
#include "tools/SelectTool.h"

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
        Document);

    Tools = std::make_unique<ToolRegistry>(*ActiveToolContext);
    Tools->Register(std::make_unique<SelectTool>());
    Tools->Register(std::make_unique<CubeTool>());
    Tools->Register(std::make_unique<CameraTool>());
    Tools->Activate("select");
}
