#include "project/TextureImportStore.h"

#include <assets/texture/TextureFormat.h>

#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>

std::string TextureSourceLocation::SourceFile() const
{
    return (std::filesystem::path(Root) / RelPath).string();
}

std::string TextureSourceLocation::MetaFile() const
{
    return SourceFile() + std::string(kImportSettingsSuffix);
}

std::string TextureSourceLocation::CookedFile() const
{
    return (std::filesystem::path(Root) / ".cooked" / (RelPath + ".stex")).string();
}

std::optional<TextureSourceLocation> ResolveTextureSource(
    std::span<const std::string> contentRoots, std::string_view virtualPath)
{
    constexpr std::string_view scheme = "asset://";
    if (!virtualPath.starts_with(scheme))
        return std::nullopt;
    const std::string rel{ virtualPath.substr(scheme.size()) };
    for (const std::string& root : contentRoots)
    {
        std::error_code ec;
        if (std::filesystem::exists(std::filesystem::path(root) / rel, ec))
            return TextureSourceLocation{ root, rel };
    }
    return std::nullopt;
}

TextureImportSettings LoadTextureImportSettingsFor(const TextureSourceLocation& source,
                                                   std::string* error)
{
    TextureImportSettings settings;
    std::ifstream file(source.MetaFile(), std::ios::binary);
    if (!file.is_open())
        return settings;
    const std::string text((std::istreambuf_iterator<char>(file)), {});
    std::string parseError;
    if (!ParseTextureImportSettings(std::as_bytes(std::span(text.data(), text.size())),
                                    settings, &parseError))
    {
        if (error != nullptr)
            *error = parseError;
        return TextureImportSettings{};
    }
    return settings;
}

bool SaveTextureImportSettingsFor(const TextureSourceLocation& source,
                                  const TextureImportSettings& settings,
                                  std::string* error)
{
    return SaveTextureImportSettingsFile(source.MetaFile(), settings, error);
}

CookedTextureState ReadCookedTextureState(const TextureSourceLocation& source)
{
    CookedTextureState state;

    std::ifstream file(source.CookedFile(), std::ios::binary);
    if (!file.is_open())
        return state;
    StexFileHeader header{};
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file.good() || !LooksLikeStex(&header, sizeof(header)))
        return state;

    state.Exists = true;
    state.Format = header.Format;
    state.Usage = header.Usage;
    state.Filter = (header.Flags & kStexFlagNearestFilter) != 0 ? TextureFilter::Nearest
                                                                : TextureFilter::Linear;
    state.Width = header.Width;
    state.Height = header.Height;
    state.MipCount = header.MipCount;
    return state;
}

std::string_view TexturePixelFormatName(TexturePixelFormat format)
{
    switch (format)
    {
    case TexturePixelFormat::RGBA8:      return "RGBA8";
    case TexturePixelFormat::RGBA8_SRGB: return "RGBA8 sRGB";
    case TexturePixelFormat::BC4:        return "BC4";
    case TexturePixelFormat::BC5:        return "BC5";
    case TexturePixelFormat::BC7:        return "BC7";
    case TexturePixelFormat::BC7_SRGB:   return "BC7 sRGB";
    case TexturePixelFormat::Unknown:
    default:                             return "unknown";
    }
}

std::string DescribeCookedTextureState(const CookedTextureState& state)
{
    if (!state.Exists)
        return "not cooked yet";
    return std::format("{}x{}, {}, {} mip{}, {} ({})",
                       state.Width, state.Height,
                       TexturePixelFormatName(state.Format),
                       state.MipCount, state.MipCount == 1 ? "" : "s",
                       state.Filter == TextureFilter::Nearest ? "nearest" : "linear",
                       TextureUsageName(state.Usage));
}
