#pragma once

#include <mutex>
#include <string>
#include <vector>

class SdlWindow;
class EditorDocument;
class CommandStack;
class SelectionService;
class MaterialLibrary;

// The document's file I/O surface: New/Open/Save/SaveAs (open and save-as go
// through the native SDL dialogs), the deferred queue that applies a dialog's
// result on the next frame (the callback fires off the frame loop), and the
// window-title reflection of the document name and dirty state.
class DocumentFileActions
{
public:
    // contentRoots are the project's content roots; empty when no project is
    // loaded (materials then scan next to the level file).
    DocumentFileActions(SdlWindow& window, EditorDocument& document, CommandStack& commands,
                        SelectionService& selection, MaterialLibrary& materials,
                        std::vector<std::string> contentRoots);

    void New();
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
    void ResetEditorState();

    SdlWindow&        Window;
    EditorDocument&    Document;
    CommandStack&     Commands;
    SelectionService& Selection;
    MaterialLibrary&  Materials;
    std::vector<std::string> ContentRoots;

    std::mutex                     PendingFileMutex;
    std::vector<PendingFileAction> PendingFileActions;
    std::string                    LastWindowTitle;
};
