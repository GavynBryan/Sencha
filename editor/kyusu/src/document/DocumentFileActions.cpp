#include "DocumentFileActions.h"

#include "EditorDocument.h"
#include "WorldDocument.h"
#include "project/MaterialLibrary.h"
#include "SceneBrushWalk.h"

#include <platform/SdlWindow.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <map>
#include <utility>

namespace
{
constexpr SDL_DialogFileFilter kDocumentFileFilters[] = {
    { "Sencha Level", "json" },
    { "All files", "*" },
};
} // namespace

DocumentFileActions::DocumentFileActions(SdlWindow& window, WorldDocument& world,
                                         std::function<void()> resetInteraction,
                                         MaterialLibrary& materials,
                                         std::vector<std::string> contentRoots)
    : Window(window)
    , World(world)
    , ResetInteraction(std::move(resetInteraction))
    , Materials(materials)
    , ContentRoots(std::move(contentRoots))
{
}

void DocumentFileActions::New()
{
    World.New();
    ResetInteraction();
}

void DocumentFileActions::Save()
{
    if (!World.FocusDocument().HasFilePath())
    {
        RequestSaveAs();
        return;
    }

    World.Save();
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
            if (World.Load(action.Path))
            {
                ResetInteraction();
                RescanMaterials(action.Path);
                LogUnresolvedFaceMaterials(action.Path);
            }
            break;
        case FileActionKind::SaveAs:
            World.SaveAs(action.Path);
            RescanMaterials(action.Path);
            break;
        }
    }
}

void DocumentFileActions::RescanMaterials(const std::string& levelPath)
{
    // Materials are project-relative: scan the project's content roots so face
    // textures resolve to the same asset:// paths the runtime will use. Without
    // a project (bare SENCHA_GAME_MODULE workflow) fall back to the directory
    // holding the level file.
    if (!ContentRoots.empty())
    {
        Materials.Rescan(ContentRoots);
        return;
    }
    const std::vector<std::string> fallback{
        std::filesystem::path(levelPath).parent_path().string()
    };
    Materials.Rescan(fallback);
}

void DocumentFileActions::LogUnresolvedFaceMaterials(const std::string& levelPath)
{
    // A face ref that no scanned root can resolve renders as the level default;
    // name each one (with a count) so the author knows what to reassign after a
    // level moves between projects.
    std::map<std::string, int> unresolved;
    ForEachVisibleBrush(World.FocusDocument().GetScene(), /*skipLocked*/ false,
        [&](EntityId, const BrushMesh& mesh, const Transform3f&)
        {
            for (const BrushFace& face : mesh.Faces)
            {
                const std::string& path = face.Material.Material.Path;
                if (path.empty())
                    continue;
                const auto& known = Materials.Materials();
                const bool found = std::any_of(known.begin(), known.end(),
                    [&](const MaterialAsset& asset) { return asset.Path == path; });
                if (!found)
                    ++unresolved[path];
            }
        });

    for (const auto& [path, count] : unresolved)
        std::fprintf(stderr, "[editor] '%s': material '%s' not found in any content root (%d face(s) fall back to the level default)\n",
                     levelPath.c_str(), path.c_str(), count);
}

void DocumentFileActions::UpdateTitle()
{
    const EditorDocument& document = World.FocusDocument();
    std::string title = "Kyusu - Level Editor - ";
    title += document.GetDisplayName();
    if (document.IsDirty())
        title += " *";

    if (title != LastWindowTitle)
    {
        Window.SetTitle(title);
        LastWindowTitle = title;
    }
}
