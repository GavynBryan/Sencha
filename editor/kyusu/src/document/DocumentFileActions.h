#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

class ConsoleRegistry;
class SdlWindow;
class WorldDocument;
class EditorDocument;
class MaterialLibrary;

// The document's file I/O surface: New/Open/Save/SaveAs (open and save-as go
// through the native SDL dialogs), the deferred queue that applies a dialog's
// result on the next frame (the callback fires off the frame loop), and the
// window-title reflection of the document name and dirty state.
class DocumentFileActions
{
public:
    // contentRoots are the project's content roots; empty when no project is
    // loaded (materials then scan next to the level file). Swapping the edited
    // document is the document's own event to raise, so nothing here has to
    // reset the editing stack: WorldDocument::OnEditedDocumentChanged does.
    // resolvePendingEdits settles open previews before a write, so no save can
    // capture half-staged geometry.
    DocumentFileActions(SdlWindow& window, WorldDocument& world,
                        std::function<void()> resolvePendingEdits,
                        MaterialLibrary& materials, std::vector<std::string> contentRoots);

    // Console surface: editor.open <path>, the scriptable half of RequestOpen.
    // An unattended cook needs to open a document without a file dialog, and
    // the console runs on the main thread, so the load is synchronous -- a
    // startup script's next command sees the document already open.
    void RegisterCommands(ConsoleRegistry& registry);

    void New();
    void NewWorld();
    // Opens the .sscene an asset:// path names, through the same deferred
    // queue as the dialogs (the caller sits inside a frame walking the
    // document the open replaces). False when no content root holds it.
    bool OpenSceneSource(std::string_view assetPath);
    void Save();
    void RequestOpen();
    void RequestSaveAs();

    // Applies any file actions a dialog callback queued (called once per frame).
    void ProcessPending();
    // Sets the window title to the document name + dirty marker when it changes.
    void UpdateTitle();

private:
    enum class FileActionKind
    {
        Open,
        SaveAs,
    };

    struct PendingFileAction
    {
        FileActionKind Kind;
        std::string    Path;
    };

    void EnqueueFileAction(FileActionKind kind, std::string path);
    void RescanMaterials(const std::string& levelPath);
    // Logs each face material ref the scanned roots cannot resolve (they render
    // as the level default), so cross-project level moves are diagnosable.
    void LogUnresolvedFaceMaterials(const std::string& levelPath);

    SdlWindow&        Window;
    WorldDocument&    World;
    std::function<void()> ResolvePendingEdits;
    MaterialLibrary&  Materials;
    std::vector<std::string> ContentRoots;

    std::mutex                     PendingFileMutex;
    std::vector<PendingFileAction> PendingFileActions;
    std::string                    LastWindowTitle;
};
