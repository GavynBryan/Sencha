#include "SelectionGlbExport.h"

#include "document/BrushBake.h"
#include "export/GltfMeshExport.h"
#include "workspace/EditorWorkspace.h"

#include <platform/SdlWindow.h>

#include <SDL3/SDL_dialog.h>

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace
{
// Self-contained export payload handed to the async save dialog: the callback
// owns it and touches no editor state.
struct GlbExportPayload
{
    MeshGeometry Geometry;
    std::vector<AssetRef> Materials;
};
}

void ExportSelectionGlb(SdlWindow& window, EditorWorkspace& workspace)
{
    if (window.GetHandle() == nullptr)
        return;

    // The geometry comes from the selection actions (baked in local space
    // through the same kernel as the level cook); the dialog is this layer's.
    const BrushMesh* mesh = workspace.Actions.SelectedExportMesh();
    if (mesh == nullptr)
    {
        std::fprintf(stderr, "[editor] export: select a brush or baked brush first\n");
        return;
    }

    auto payload = std::make_unique<GlbExportPayload>();
    std::string error;
    if (!BakeBrushToGeometry(*mesh, workspace.ActiveDocument().GetDefaultMaterial(),
                             payload->Geometry, payload->Materials, &error))
    {
        std::fprintf(stderr, "[editor] export: %s\n", error.c_str());
        return;
    }

    static constexpr SDL_DialogFileFilter kGlbFilters[] = { { "Binary glTF", "glb" } };
    SDL_ShowSaveFileDialog(
        [](void* userdata, const char* const* filelist, int)
        {
            // The dialog callback may run off the main thread; the payload is
            // self-contained (no editor state), so writing here is safe.
            std::unique_ptr<GlbExportPayload> owned(static_cast<GlbExportPayload*>(userdata));
            if (filelist == nullptr || filelist[0] == nullptr)
                return;
            std::filesystem::path path(filelist[0]);
            if (path.extension() != ".glb")
                path += ".glb";
            std::string writeError;
            if (!WriteGlbFile(owned->Geometry, owned->Materials, path, &writeError))
                std::fprintf(stderr, "[editor] export: %s\n", writeError.c_str());
            else
                std::fprintf(stderr, "[editor] exported '%s'\n", path.string().c_str());
        },
        payload.release(),
        window.GetHandle(),
        kGlbFilters,
        static_cast<int>(std::size(kGlbFilters)),
        nullptr);
}
