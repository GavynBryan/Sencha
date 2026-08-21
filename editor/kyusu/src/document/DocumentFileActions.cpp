#include "DocumentFileActions.h"

#include <core/console/ConsoleRegistry.h>
#include <core/console/ConsoleTypes.h>

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
    { "Sencha World", "sworld" },
    { "All files", "*" },
};

bool IsWorldPath(std::string_view path)
{
    constexpr std::string_view kExtension = ".sworld";
    return path.size() >= kExtension.size()
        && path.substr(path.size() - kExtension.size()) == kExtension;
}
} // namespace

DocumentFileActions::DocumentFileActions(SdlWindow& window, WorldDocument& world,
                                         std::function<void()> resolvePendingEdits,
                                         MaterialLibrary& materials,
                                         std::vector<std::string> contentRoots)
    : Window(window)
    , World(world)
    , ResolvePendingEdits(std::move(resolvePendingEdits))
    , Materials(materials)
    , ContentRoots(std::move(contentRoots))
{
}

void DocumentFileActions::RegisterCommands(ConsoleRegistry& registry)
{
    registry.RegisterCommand({
        .Name = "editor.open",
        .Owner = "editor",
        .Usage = "editor.open <path>",
        .Help = "Open a level or world document by path, without the file "
                "dialog. Synchronous: the document is open when this returns, "
                "so a startup script can cook it with the next command.",
        .Callback = [this](ConsoleExecutionContext&,
                           std::span<const std::string> args) {
            ConsoleResult result;
            if (args.size() != 1)
            {
                result.Status = ConsoleStatus::InvalidArguments;
                result.Error("expected exactly one path");
                return result;
            }
            const std::string& path = args[0];
            const bool loaded = IsWorldPath(path)
                ? World.LoadWorld(path)
                : World.Load(path);
            if (!loaded)
            {
                result.Status = ConsoleStatus::ExecutionFailed;
                result.Error("could not open '" + path + "'");
                return result;
            }
            RescanMaterials(path);
            LogUnresolvedFaceMaterials(path);
            UpdateTitle();
            result.Info("opened '" + path + "'");
            return result;
        },
    });
}

void DocumentFileActions::New()
{
    World.New();
}

void DocumentFileActions::NewWorld()
{
    World.NewWorld("Untitled World");
}

void DocumentFileActions::Save()
{
    if (!World.HasSaveTarget())
    {
        RequestSaveAs();
        return;
    }

    ResolvePendingEdits();
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
        {
            const bool loaded = IsWorldPath(action.Path)
                ? World.LoadWorld(action.Path)
                : World.Load(action.Path);
            if (loaded)
            {
                RescanMaterials(action.Path);
                LogUnresolvedFaceMaterials(action.Path);
            }
            break;
        }
        case FileActionKind::SaveAs:
            ResolvePendingEdits();
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
    std::string title = "Kyusu - Level Editor - ";
    if (World.IsWorld())
    {
        title += World.Manifest().Name;
        const ZoneId focus = World.FocusZone();
        for (const ZoneHeader& zone : World.Manifest().Zones)
        {
            if (zone.Id != focus)
                continue;
            title += " : ";
            title += zone.Name;
            break;
        }
        if (World.IsDirty())
            title += " *";
    }
    else
    {
        const EditorDocument& document = World.FocusDocument();
        title += document.GetDisplayName();
        if (document.IsDirty())
            title += " *";
    }

    if (title != LastWindowTitle)
    {
        Window.SetTitle(title);
        LastWindowTitle = title;
    }
}
