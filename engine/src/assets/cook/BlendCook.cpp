#include <assets/cook/BlendCook.h>

#include <core/io/FileBytes.h>

#include <assets/cook/MeshCook.h>
#include <core/hash/ContentHash.h>

#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    std::string BlenderExecutable()
    {
        const char* override = std::getenv("SENCHA_BLENDER");
        return override != nullptr && override[0] != '\0' ? override : "blender";
    }

    // Scoped temp directory for the .blend -> .glb round trip; best-effort
    // removal (leaking a temp dir on a dev machine beats failing the cook).
    struct ScopedTempDir
    {
        std::filesystem::path Path;

        ~ScopedTempDir()
        {
            if (!Path.empty())
            {
                std::error_code ec;
                std::filesystem::remove_all(Path, ec);
            }
        }
    };

    bool WriteFileBytes(const std::filesystem::path& path, std::span<const std::byte> bytes)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
            return false;
        if (!bytes.empty())
            file.write(reinterpret_cast<const char*>(bytes.data()),
                       static_cast<std::streamsize>(bytes.size()));
        return file.good();
    }

} // namespace

std::vector<std::string_view> BlendMeshImporter::SourceExtensions() const
{
    return { ".blend" };
}

ImportResult BlendMeshImporter::Import(const ImportInput& input, ICookOutputWriter& output)
{
    std::error_code ec;
    const std::filesystem::path tempRoot = std::filesystem::temp_directory_path(ec);
    if (ec)
        return ImportResult{ .Error = "blend import: no temp directory available" };

    ScopedTempDir temp;
    temp.Path = tempRoot
        / std::format("sencha-blend-cook-{:016x}", HashBytes64(input.SourceRelPath));
    std::filesystem::create_directories(temp.Path, ec);
    if (ec)
        return ImportResult{ .Error = "blend import: could not create temp directory" };

    const std::filesystem::path blendPath = temp.Path / "source.blend";
    const std::filesystem::path glbPath = temp.Path / "export.glb";
    if (!WriteFileBytes(blendPath, input.Bytes))
        return ImportResult{ .Error = "blend import: could not stage .blend to temp file" };

    // export_apply bakes modifiers. Textures stay out of the .glb: the texture
    // pipeline owns images, this cook owns geometry.
    //
    // Tangents are exported only for a skinned source, and the asymmetry is a
    // contract rather than a preference. A static mesh gets cook-side
    // MikkTSpace tangents (Decision M), which de-index and re-weld vertices; a
    // skinned mesh cannot, because that re-weld would desync the influence
    // stream running parallel to the vertices, so the glTF importer rejects a
    // skinned primitive without authored TANGENT. Exporting them
    // unconditionally would instead change every static mesh's cooked bytes.
    // The decision needs the scene, so Blender makes it: one expression, no
    // second invocation to inspect the file.
    const std::string pythonExpr = std::format(
        "import bpy; "
        "skinned=any(o.type=='ARMATURE' for o in bpy.data.objects); "
        "bpy.ops.export_scene.gltf("
        "filepath=r'{}', export_format='GLB', export_apply=True, "
        "export_image_format='NONE', export_tangents=skinned)",
        glbPath.generic_string());

#ifdef _WIN32
    constexpr std::string_view kQuiet = "> NUL 2>&1";
#else
    constexpr std::string_view kQuiet = "> /dev/null 2>&1";
#endif
    const std::string command = std::format(
        "\"{}\" --background --factory-startup \"{}\" --python-exit-code 1 --python-expr \"{}\" {}",
        BlenderExecutable(), blendPath.generic_string(), pythonExpr, kQuiet);

    if (std::system(command.c_str()) != 0)
    {
        return ImportResult{ .Error = std::format(
            "blend import: headless Blender export failed for '{}' (is Blender installed? "
            "set SENCHA_BLENDER to the executable if it is not on PATH)",
            input.SourceRelPath) };
    }

    std::vector<std::byte> glbBytes;
    if (!ReadFileBytes(glbPath, glbBytes))
        return ImportResult{ .Error = "blend import: Blender produced no .glb output" };

    // Everything funnels through the one glTF import path (Decision B); the
    // .blend source's rel-path keeps artifact naming on the authored file.
    GltfMeshImporter gltfImporter;
    ImportResult result = gltfImporter.Import(ImportInput{ input.SourceRelPath, glbBytes }, output);
    if (!result.IsValid())
        result.Error = "blend import: " + result.Error;
    return result;
}
