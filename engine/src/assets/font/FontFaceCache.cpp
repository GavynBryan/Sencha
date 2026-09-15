#include <assets/font/FontFaceCache.h>

#include <utility>

FontFaceCache::FontFaceCache(LoggingProvider& logging)
    : Log(logging.GetLogger<FontFaceCache>())
{
    ReserveNullSlot();
}

FontFaceCache::~FontFaceCache()
{
    FreeAllEntries();
}

FontFaceHandle FontFaceCache::Register(std::string_view path, FontFace face)
{
    if (!face.IsValid())
    {
        Log.Error("FontFaceCache: refusing to register invalid face '{}'", path);
        return {};
    }

    FontFaceEntry entry{};
    entry.Face = std::move(face);
    return AllocNamedHandle(path, std::move(entry));
}

FontFaceHandle FontFaceCache::Find(std::string_view path) const
{
    return FindRegisteredHandle(path);
}

std::string_view FontFaceCache::GetName(FontFaceHandle handle) const
{
    return GetRegisteredPath(handle);
}

const FontFace* FontFaceCache::Get(FontFaceHandle handle) const
{
    const FontFaceEntry* entry = Resolve(handle);
    return entry != nullptr ? &entry->Face : nullptr;
}

// -- AssetCache CRTP hooks ---------------------------------------------------

void FontFaceCache::OnFree(FontFaceEntry& entry)
{
    entry.Face = {};
}

bool FontFaceCache::IsEntryLive(const FontFaceEntry& entry) const
{
    return entry.Face.IsValid();
}
