#pragma once

#include "PendingPlacement.h"

#include <memory>
#include <optional>
#include <string>

class CommandStack;
class EditorDocument;
class PlacementSession;
class SelectionService;
struct ICommand;

// Why `document` cannot take `subject`; empty when it can.
[[nodiscard]] std::string PlacementRefusal(const PlacementSubject& subject, EditorDocument& document);

// The one step that makes a pending placement, selecting what it put down;
// null when the document refuses it. Every way of placing builds it here.
[[nodiscard]] std::unique_ptr<ICommand> MakePlacementCommand(const PendingPlacement& pending,
                                                             EditorDocument& document,
                                                             SelectionService& selection);

// Commits the session's pending placement and makes it, as one step. What was
// placed, or nullopt when nothing was pending or the document refused it.
std::optional<PendingPlacement> PlacePending(PlacementSession& session, EditorDocument& document,
                                             SelectionService& selection, CommandStack& commands);
