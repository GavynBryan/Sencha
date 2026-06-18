#include "LevelWorkspace.h"

#include "tools/CameraTool.h"
#include "tools/BrushTool.h"
#include "tools/SelectTool.h"

#include <memory>
#include <utility>

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
        MeshEdit,
        Marquee,
        Grid);

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
    Sink = std::make_unique<BrushManipulationSink>(Document.GetScene(), Document, commands);

    // The session reads selection and element mode live on each pointer-down, so
    // it never needs rebuilding when the selection or mode changes. It consumes a
    // click only when a manipulator is hit; otherwise the select tool picks.
    auto session = std::make_unique<ManipulatorSession>(Selection, MeshEdit, *Sink, Grid);
    Manipulators = session.get();
    Sessions.SetSession(std::move(session));
}
