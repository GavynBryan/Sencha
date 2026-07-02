#include "DocumentFileActions.h"

#include "EditorDocument.h"
#include "MaterialLibrary.h"
#include "commands/CommandStack.h"
#include "selection/SelectionService.h"

#include <platform/SdlWindow.h>

#include <SDL3/SDL.h>

#include <filesystem>
#include <utility>

namespace
{
constexpr SDL_DialogFileFilter kDocumentFileFilters[] = {
    { "Sencha Level", "json" },
    { "All files", "*" },
};
} // namespace

DocumentFileActions::DocumentFileActions(SdlWindow& window, EditorDocument& document, CommandStack& commands,
                                         SelectionService& selection, MaterialLibrary& materials)
    : Window(window)
    , Document(document)
    , Commands(commands)
    , Selection(selection)
    , Materials(materials)
{
}

void DocumentFileActions::New()
{
    Document.New();
    ResetEditorState();
}

void DocumentFileActions::Save()
{
    if (!Document.HasFilePath())
    {
        RequestSaveAs();
        return;
    }

    Document.Save();
}

void DocumentFileActions::RequestOpen()
{
    if (Window.GetHandle() == nullptr)
        return;

    SDL_ShowOpenFileDialog(
        [](void* userdata, const char* const* filelist, int)
        {
            auto* self = static_cast<DocumentFileActions*>(userdata);
            if (filelist != nullptr && filelist[0] != nullptr)
                self->EnqueueFileAction(FileActionKind::Open, filelist[0]);
        },
        this,
        Window.GetHandle(),
        kDocumentFileFilters,
        static_cast<int>(std::size(kDocumentFileFilters)),
        nullptr,
        false);
}

void DocumentFileActions::RequestSaveAs()
{
    if (Window.GetHandle() == nullptr)
        return;

    SDL_ShowSaveFileDialog(
        [](void* userdata, const char* const* filelist, int)
        {
            auto* self = static_cast<DocumentFileActions*>(userdata);
            if (filelist != nullptr && filelist[0] != nullptr)
                self->EnqueueFileAction(FileActionKind::SaveAs, filelist[0]);
        },
        this,
        Window.GetHandle(),
        kDocumentFileFilters,
        static_cast<int>(std::size(kDocumentFileFilters)),
        nullptr);
}

void DocumentFileActions::EnqueueFileAction(FileActionKind kind, std::string path)
{
    const std::scoped_lock lock(PendingFileMutex);
    PendingFileActions.push_back({ kind, std::move(path) });
}

void DocumentFileActions::ProcessPending()
{
    std::vector<PendingFileAction> actions;
    {
        const std::scoped_lock lock(PendingFileMutex);
        actions.swap(PendingFileActions);
    }

    for (const PendingFileAction& action : actions)
    {
        switch (action.Kind)
        {
        case FileActionKind::Open:
            if (Document.Load(action.Path))
            {
                ResetEditorState();
                RescanMaterials(action.Path);
            }
            break;
        case FileActionKind::SaveAs:
            Document.SaveAs(action.Path);
            RescanMaterials(action.Path);
            break;
        }
    }
}

void DocumentFileActions::RescanMaterials(const std::string& levelPath)
{
    // Materials are project-relative: scan the directory holding the level file
    // so face textures resolve to the same asset:// paths the runtime will use.
    const std::filesystem::path dir = std::filesystem::path(levelPath).parent_path();
    Materials.Rescan(dir.string());
}

void DocumentFileActions::ResetEditorState()
{
    Commands.Clear();
    Selection.ClearSelection();
}

void DocumentFileActions::UpdateTitle()
{
    std::string title = "Kyusu - Level Editor - ";
    title += Document.GetDisplayName();
    if (Document.IsDirty())
        title += " *";

    if (title != LastWindowTitle)
    {
        Window.SetTitle(title);
        LastWindowTitle = title;
    }
}
