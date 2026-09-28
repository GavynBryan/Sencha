#pragma once

#include "documents/DocumentRef.h"

#include <app/Engine.h>

#include <functional>
#include <optional>

class DocumentSourceSet;
class EditorUiFeature;

// Exit waits while any open document has changes, an open edit included.
[[nodiscard]] Engine::ExitDecision DecideDocumentExit(const DocumentSourceSet& sources);

// Undo, redo, save and save all on the shell, and a prompt that holds exit until
// every change is saved or deliberately discarded. The host clears
// Engine::OnExitRequested before `sources` is destroyed.
void InstallDocumentShellActions(EditorUiFeature& ui, Engine& engine, DocumentSourceSet& sources,
                                 std::function<std::optional<DocumentRef>()> activeDocument);
