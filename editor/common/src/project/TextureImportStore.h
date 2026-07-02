#pragma once

#include <assets/cook/TextureImportSettings.h>
#include <render/TextureData.h>

#include <optional>
#include <span>
#include <string>
#include <string_view>

//=============================================================================
// TextureImportStore: the file-level half of the texture import pipeline,
// shared by editor UIs and headless tests.
//
// Resolves a texture's SOURCE file from its virtual path (never from a
// registry record: the cooked overlay repoints record FilePaths at .cooked/,
// and import settings live beside the source, outside the pruned cache),
// loads/saves the "<source>.meta" sidecar, and reads the cooked artifact's
// header so a UI can show ground truth (what the cook actually produced)
// instead of what the author hopes it produced.
//=============================================================================

struct TextureSourceLocation
{
    std::string Root;    // the content root that owns the source
    std::string RelPath; // root-relative source path ("textures/dev/foo.png")

    [[nodiscard]] std::string SourceFile() const;
    [[nodiscard]] std::string MetaFile() const;
    [[nodiscard]] std::string CookedFile() const; // .cooked/<rel>.stex
};

// "asset://<rel>" -> the content root where <rel> exists on disk.
[[nodiscard]] std::optional<TextureSourceLocation> ResolveTextureSource(
    std::span<const std::string> contentRoots, std::string_view virtualPath);

// Sidecar read; a missing sidecar is the defaults. A malformed one also
// yields defaults but reports through *error so the UI can say so.
[[nodiscard]] TextureImportSettings LoadTextureImportSettingsFor(
    const TextureSourceLocation& source, std::string* error = nullptr);

[[nodiscard]] bool SaveTextureImportSettingsFor(const TextureSourceLocation& source,
                                                const TextureImportSettings& settings,
                                                std::string* error = nullptr);

// What the cooked artifact actually is, read from the .stex header.
struct CookedTextureState
{
    bool Exists = false;
    TexturePixelFormat Format = TexturePixelFormat::Unknown;
    TextureUsage Usage = TextureUsage::Unknown;
    TextureFilter Filter = TextureFilter::Linear;
    uint32_t Width = 0;
    uint32_t Height = 0;
    uint32_t MipCount = 0;
};

[[nodiscard]] CookedTextureState ReadCookedTextureState(const TextureSourceLocation& source);

// Display helpers for UI/status text.
[[nodiscard]] std::string_view TexturePixelFormatName(TexturePixelFormat format);
[[nodiscard]] std::string DescribeCookedTextureState(const CookedTextureState& state);
