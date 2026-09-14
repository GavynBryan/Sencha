#pragma once

#include <assets/font/FontFace.h>
#include <assets/font/FontFaceHandle.h>
#include <core/assets/AssetCache.h>
#include <core/handle/Owned.h>
#include <core/logging/LoggingProvider.h>

#include <cstdint>
#include <string>

struct FontFaceEntry
{
    FontFace Face;
    std::uint32_t Generation = 0;
    std::uint32_t RefCount = 0;
    std::string PathKey;
};

using FontFaceCacheHandle = Owned<FontFaceHandle>;

//=============================================================================
// FontFaceCache
//
// Path-keyed, ref-counted residency for cooked font faces. CPU-only: a face is
// bytes plus its registration metadata, and nothing here rasterises anything.
// Glyph atlases belong to whichever runtime draws text at the size a document
// actually asks for, and are not the same lifetime as the face.
//
// Faces are shared. Several packages naming the same family resolve to one
// entry, which is the point of keying on the asset path.
//=============================================================================
class FontFaceCache : public AssetCache<FontFaceCache, FontFaceHandle, FontFaceEntry, AssetType::Font>
{
public:
    explicit FontFaceCache(LoggingProvider& logging);
    ~FontFaceCache() override;

    FontFaceCache(const FontFaceCache&) = delete;
    FontFaceCache& operator=(const FontFaceCache&) = delete;
    FontFaceCache(FontFaceCache&&) = delete;
    FontFaceCache& operator=(FontFaceCache&&) = delete;

    // Registers a parsed face under `path` (refcount 1, owned by the caller).
    // An already-registered path gains a reference and `face` is discarded --
    // first registration wins, the dedup contract every cache shares.
    [[nodiscard]] FontFaceHandle Register(std::string_view path, FontFace face);

    [[nodiscard]] FontFaceHandle Find(std::string_view path) const;
    [[nodiscard]] std::string_view GetName(FontFaceHandle handle) const;

    // Null if the handle is invalid or released.
    [[nodiscard]] const FontFace* Get(FontFaceHandle handle) const;

private:
    friend class AssetCache<FontFaceCache, FontFaceHandle, FontFaceEntry, AssetType::Font>;

    void OnFree(FontFaceEntry& entry);
    bool IsEntryLive(const FontFaceEntry& entry) const;

    Logger& Log;
};
