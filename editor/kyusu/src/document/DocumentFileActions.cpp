#include "DocumentFileActions.h"

#include <core/console/ConsoleRegistry.h>
#include <core/console/ConsoleTypes.h>

#include "EditorDocument.h"
#include "WorldDocument.h"
#include "project/MaterialLibrary.h"
#include "selection/SelectionService.h"
#include "meshedit/MeshEditService.h"
#include "brush/BrushWorkCounters.h"
#include "SceneBrushWalk.h"
#include "scene_source/SceneSourcePaths.h"

#include <platform/SdlWindow.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/registry/Registry.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
constexpr SDL_DialogFileFilter kDocumentFileFilters[] = {
    { "Sencha Scene", "sscene" },
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
                                         std::vector<std::string> contentRoots,
                                         SelectionService& selection,
                                         MeshEditService& meshEdit)
    : Window(window)
    , World(world)
    , ResolvePendingEdits(std::move(resolvePendingEdits))
    , Materials(materials)
    , ContentRoots(std::move(contentRoots))
    , Selection(selection)
    , MeshEdit(meshEdit)
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
    registry.RegisterCommand({
        .Name = "editor.select",
        .Owner = "editor",
        .Usage = "editor.select <persistent-id-hex> [face|edge|vertex <index>]",
        .Help = "Replace the selection with the focus document's entity carrying "
                "the given persistent id (the hex id a scene file names), or one "
                "of its mesh elements.",
        .Callback = [this](ConsoleExecutionContext&,
                           std::span<const std::string> args) {
            ConsoleResult result;
            if (args.size() != 1 && args.size() != 3)
            {
                result.Status = ConsoleStatus::InvalidArguments;
                result.Error("expected <persistent id> [face|edge|vertex <index>]");
                return result;
            }
            std::uint64_t value = 0;
            try
            {
                value = std::stoull(args[0], nullptr, 16);
            }
            catch (const std::exception&)
            {
                result.Status = ConsoleStatus::InvalidArguments;
                result.Error("'" + args[0] + "' is not a hex persistent id");
                return result;
            }
            const EditorDocument& document = World.FocusDocument();
            const EditorScene& scene = document.GetScene();
            const ::World& components = document.GetRegistry().Components;
            for (const EntityId entity : scene.GetAllEntities())
            {
                const auto* id = components.TryGet<PersistentIdComponent>(entity);
                if (id == nullptr || id->Id.Value != value)
                    continue;
                const RegistryId registryId = document.GetRegistry().Id;
                SelectableRef ref = SelectableRef::EntitySelection(registryId, entity);
                if (args.size() == 3)
                {
                    std::uint32_t element = 0;
                    try
                    {
                        element = static_cast<std::uint32_t>(std::stoul(args[2]));
                    }
                    catch (const std::exception&)
                    {
                        result.Status = ConsoleStatus::InvalidArguments;
                        result.Error("'" + args[2] + "' is not an element index");
                        return result;
                    }
                    if (args[1] == "face")
                        ref = SelectableRef::FaceSelection(registryId, entity, element);
                    else if (args[1] == "edge")
                        ref = SelectableRef::EdgeSelection(registryId, entity, element);
                    else if (args[1] == "vertex")
                        ref = SelectableRef::VertexSelection(registryId, entity, element);
                    else
                    {
                        result.Status = ConsoleStatus::InvalidArguments;
                        result.Error("element kind must be face, edge or vertex");
                        return result;
                    }
                }
                Selection.SetSelection({ ref });
                result.Info("selected '" + args[0] + "'");
                return result;
            }
            result.Status = ConsoleStatus::ExecutionFailed;
            result.Error("no entity with persistent id '" + args[0] + "'");
            return result;
        },
    });
    registry.RegisterCommand({
        .Name = "editor.mode",
        .Owner = "editor",
        .Usage = "editor.mode <object|face|edge|vertex>",
        .Help = "Set the mesh element edit mode, as the toolbar would.",
        .Callback = [this](ConsoleExecutionContext&,
                           std::span<const std::string> args) {
            ConsoleResult result;
            if (args.size() != 1)
            {
                result.Status = ConsoleStatus::InvalidArguments;
                result.Error("expected one mode");
                return result;
            }
            const std::string& mode = args[0];
            if (mode == "object")
                MeshEdit.SetElementKind(MeshElementKind::Object);
            else if (mode == "face")
                MeshEdit.SetElementKind(MeshElementKind::Face);
            else if (mode == "edge")
                MeshEdit.SetElementKind(MeshElementKind::Edge);
            else if (mode == "vertex")
                MeshEdit.SetElementKind(MeshElementKind::Vertex);
            else
            {
                result.Status = ConsoleStatus::InvalidArguments;
                result.Error("mode must be object, face, edge or vertex");
                return result;
            }
            result.Info("mode " + mode);
            return result;
        },
    });
    registry.RegisterCommand({
        .Name = "editor.brush.counters",
        .Owner = "editor",
        .Usage = "editor.brush.counters",
        .Help = "Last frame's brush geometry work: interactive reconstruction "
                "(zero on an idle unchanged scene) and intentional flattening.",
        .Callback = [](ConsoleExecutionContext&, std::span<const std::string>) {
            const BrushWorkCounters& c = BrushWorkCounters::LastFrame();
            ConsoleResult result;
            result.Info("interactive: evaluations " + std::to_string(c.Evaluations)
                        + " placement_rebuilds " + std::to_string(c.PlacementRebuilds)
                        + " element_builds " + std::to_string(c.ElementBuilds)
                        + " draw_record_rebuilds " + std::to_string(c.DrawRecordRebuilds)
                        + " bakes " + std::to_string(c.Bakes)
                        + " piece_walks " + std::to_string(c.PieceWalks));
            result.Info("intentional: cook_collects " + std::to_string(c.CookCollects)
                        + " export_flattens " + std::to_string(c.ExportFlattens)
                        + " merge_flattens " + std::to_string(c.MergeFlattens)
                        + " bake_flattens " + std::to_string(c.BakeFlattens));
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

bool DocumentFileActions::OpenSceneSource(std::string_view assetPath)
{
    std::vector<std::filesystem::path> roots(ContentRoots.begin(), ContentRoots.end());
    const std::filesystem::path file = ResolveSceneSourceFile(roots, assetPath);
    if (file.empty())
        return false;
    EnqueueFileAction(FileActionKind::Open, file.string());
    return true;
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
    ForEachVisibleBrushSource(World.FocusDocument().GetScene(), /*skipLocked*/ false,
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

std::string DocumentFileActions::DocumentLabel() const
{
    std::string label;
    if (World.IsWorld())
    {
        label += World.Manifest().Name;
        const ZoneId focus = World.FocusZone();
        for (const ZoneHeader& zone : World.Manifest().Zones)
        {
            if (zone.Id != focus)
                continue;
            label += " : ";
            label += zone.Name;
            break;
        }
        if (World.IsDirty())
            label += " *";
    }
    else
    {
        const EditorDocument& document = World.FocusDocument();
        label += document.GetDisplayName();
        if (document.IsDirty())
            label += " *";
    }
    return label;
}

void DocumentFileActions::UpdateTitle()
{
    const std::string title = "Kyusu - Level Editor - " + DocumentLabel();
    if (title != LastWindowTitle)
    {
        Window.SetTitle(title);
        LastWindowTitle = title;
    }
}
