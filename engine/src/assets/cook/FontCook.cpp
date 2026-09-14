#include <assets/cook/FontCook.h>

#include <assets/cook/FontImportSettings.h>
#include <assets/font/FontFace.h>
#include <assets/font/FontFaceSerializer.h>

#include <string>

std::vector<std::string_view> FontFaceImporter::SourceExtensions() const
{
    return { ".ttf", ".otf" };
}

ImportResult FontFaceImporter::Import(const ImportInput& input, ICookOutputWriter& output)
{
    if (input.Bytes.empty())
        return ImportResult{ .Error = "font import: empty source" };

    FontImportSettings settings;
    std::string settingsError;
    if (!ParseFontImportSettings(input.MetaBytes, settings, &settingsError))
        return ImportResult{ .Error = "font import: " + settingsError };

    FontFace face;
    InferFontFaceFromFileName(input.SourceRelPath, face.Family, face.Weight, face.Style);
    if (settings.Family) face.Family = *settings.Family;
    if (settings.Weight) face.Weight = *settings.Weight;
    if (settings.Style) face.Style = *settings.Style;
    if (settings.Fallback) face.Fallback = *settings.Fallback;

    if (face.Family.empty())
        return ImportResult{ .Error = "font import: source has no filename to take a family from" };

    face.Bytes.assign(input.Bytes.begin(), input.Bytes.end());

    std::vector<std::byte> sfontBytes;
    if (!WriteSfontToBytes(face, sfontBytes))
        return ImportResult{ .Error = "font import: sfont serialization failed" };

    CookedArtifact artifact;
    artifact.Path = "asset://" + std::string(input.SourceRelPath);
    artifact.FileRelPath = ".cooked/" + std::string(input.SourceRelPath) + ".sfont";
    artifact.Type = AssetType::Font;

    if (!output.WriteBytes(artifact.FileRelPath, sfontBytes))
        return ImportResult{ .Error = "font import: artifact write failed" };

    ImportResult result;
    result.Artifacts.push_back(std::move(artifact));
    return result;
}
