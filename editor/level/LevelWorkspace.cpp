#include "LevelWorkspace.h"

#include "commands/DeleteEntityCommand.h"
#include "tools/CameraTool.h"
#include "tools/BrushTool.h"
#include "tools/SelectTool.h"

#include <memory>
#include <utility>
#include <vector>

LevelWorkspace::LevelWorkspace(LoggingProvider& logging)
    : Document(logging)
    , Selection(LevelSelection)
{
}

void LevelWorkspace::Init(CommandStack& commands)
{
    Commands = &commands;

    ActiveToolContext = std::make_unique<ToolContext>(
        commands,
        Selection,
        Picking,
        Document.GetScene(),
        Document,
        Interactions,
        Preview,
        MeshEdit,
        Marquee,
        Grid,
        BrushCreate);

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

    // All scene mutation during manipulation goes through this one sink; the
    // session and manipulators stay scene-agnostic.
    Sink = std::make_unique<BrushManipulationSink>(Document.GetScene(), Document, commands, Selection);

    // The session reads selection and element mode live on each pointer-down, so
    // it never needs rebuilding when the selection or mode changes. It consumes a
    // click only when a manipulator is hit; otherwise the select tool picks.
    auto session = std::make_unique<ManipulatorSession>(Selection, MeshEdit, *Sink, Grid);
    Manipulators = session.get();
    Sessions.SetSession(std::move(session));
}

void LevelWorkspace::DeleteSelection()
{
    if (Commands == nullptr)
        return;

    // Entity-kind selections only; vertex/edge/face element refs are not entities.
    std::vector<EntityId> entities;
    for (const SelectableRef& ref : Selection.GetSelection())
        if (ref.Kind == SelectableKind::Entity && ref.Entity.IsValid())
            entities.push_back(ref.Entity);

    if (entities.empty())
        return;

    Commands->Execute(MakeDeleteEntitiesCommand(entities, Document.GetScene(), Document, Selection));
}
