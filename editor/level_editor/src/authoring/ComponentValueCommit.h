#pragma once

#include "EditorComponentAdapter.h"
#include "commands/CommandStack.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/commands/ValueCommand.h"

#include <memory>

// For a small trivially copyable component holding no handles.
template <typename Component>
PropertyEditResult CommitComponentValue(EditorComponentInspectorContext& context, Component before,
                                        const Component& after)
{
    context.Scene.SetComponent(context.Entity, after);
    context.Commands.Execute(std::make_unique<ValueCommand<Component>>(
        before, after,
        [&scene = context.Scene, entity = context.Entity](const Component& value) { scene.SetComponent(entity, value); },
        context.Document));
    return PropertyEditResult::Applied;
}
