#pragma once

#include "commands/ICommand.h"

#include <ecs/EntityId.h>
#include <zone/WorldPartitionIds.h>

#include <memory>

class EditorDocument;

// Rewrites a portal's Transition field as an undoable step (the raw-bytes
// component edit the inspector uses). Returns nullptr when the entity carries
// no portal component. Manifest transition records are non-undoable verbs;
// this link is the one undoable half of transition authoring.
[[nodiscard]] std::unique_ptr<ICommand> MakeLinkPortalCommand(EditorDocument& document,
                                                              EntityId entity,
                                                              TransitionId transition);
