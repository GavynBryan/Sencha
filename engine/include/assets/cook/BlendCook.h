#pragma once

#include <assets/cook/AssetImporter.h>

//=============================================================================
// .blend cook front end (docs/assets/pipeline.md, Decision B). Dev-only —
// compiled under SENCHA_ENABLE_COOK, never shipped.
//
// .blend does not get its own parser: the importer shells out to headless
// Blender ("blender --background" + a glTF export expression) to produce a
// self-contained .glb, then funnels through the one glTF import path
// (MeshCook.h). Blender is a dev-machine dependency of this cook step only —
// the same never-ships rule as glslang; a machine without it fails this
// importer with a clear message and the driver isolates the failure to the
// .blend sources.
//
// The shell-out is the one importer that cannot be pure: it writes the
// source bytes to a temp file (Blender opens files, not memory), runs a
// subprocess, and reads the exported .glb back. Everything after that — the
// glTF parse, tangent generation, artifact naming under the .blend source's
// virtual path — is the shared pure path.
//
// The executable is "blender" on PATH, overridable via the SENCHA_BLENDER
// environment variable.
//=============================================================================
class BlendMeshImporter final : public IAssetImporter
{
public:
    [[nodiscard]] std::vector<std::string_view> SourceExtensions() const override;
    [[nodiscard]] ImportResult Import(const ImportInput& input,
                                      ICookOutputWriter& output) override;
};
