#include "RmlPackageFileSource.h"

#include <assets/ui/UiPackage.h>
#include <core/logging/Logger.h>

#include <algorithm>
#include <cstring>

RmlPackageFileSource::RmlPackageFileSource(Logger& log)
    : Log(log)
{
}

RmlPackageFileSource::ActivePackageScope::ActivePackageScope(RmlPackageFileSource& source,
                                                            const UiPackage& package)
    : Source(source)
{
    Source.Active = &package;
}

RmlPackageFileSource::ActivePackageScope::~ActivePackageScope()
{
    Source.Active = nullptr;
}

bool RmlPackageFileSource::ReplaceHostFile(std::string_view name, std::vector<std::byte> bytes)
{
    const std::string key(name);
    if (bytes.empty())
        return HostFiles.erase(key) > 0;

    const auto existing = HostFiles.find(key);
    if (existing != HostFiles.end() && existing->second == bytes)
        return false;

    HostFiles[key] = std::move(bytes);
    return true;
}

Rml::FileHandle RmlPackageFileSource::Open(const Rml::String& path)
{
    if (Active == nullptr)
    {
        // A request outside a load scope means something asked for a file after
        // the document was built -- a lazily-resolved resource. Those belong in
        // the package's resource table, resolved as assets, not fetched here.
        Log.Error("ui: '{}' was requested outside a package load; "
                  "resources belong in the package resource table", path);
        return 0;
    }

    const std::vector<std::byte>* bytes = nullptr;
    if (const auto host = HostFiles.find(std::string(path)); host != HostFiles.end())
    {
        bytes = &host->second;
    }
    else if (const UiPackageBlob* blob = Active->FindBlob(path); blob != nullptr)
    {
        bytes = &blob->Bytes;
    }
    else if (Resources == nullptr || !Resources->ResolveResourceBytes(path, bytes))
    {
        Log.Error("ui: package '{}' neither carries nor declares '{}'",
                  Active->RootDocumentName, path);
        return 0;
    }

    const std::uint64_t handle = NextHandle++;
    Files.emplace(handle, OpenFile{ bytes, 0 });
    return static_cast<Rml::FileHandle>(handle);
}

void RmlPackageFileSource::Close(Rml::FileHandle file)
{
    Files.erase(static_cast<std::uint64_t>(file));
}

RmlPackageFileSource::OpenFile* RmlPackageFileSource::Resolve(Rml::FileHandle file)
{
    const auto it = Files.find(static_cast<std::uint64_t>(file));
    return it != Files.end() ? &it->second : nullptr;
}

size_t RmlPackageFileSource::Read(void* buffer, size_t size, Rml::FileHandle file)
{
    OpenFile* open = Resolve(file);
    if (open == nullptr || open->Bytes == nullptr)
        return 0;

    const std::size_t remaining = open->Bytes->size() - open->Cursor;
    const std::size_t count = std::min(size, remaining);
    if (count > 0)
    {
        std::memcpy(buffer, open->Bytes->data() + open->Cursor, count);
        open->Cursor += count;
    }
    return count;
}

bool RmlPackageFileSource::Seek(Rml::FileHandle file, long offset, int origin)
{
    OpenFile* open = Resolve(file);
    if (open == nullptr || open->Bytes == nullptr)
        return false;

    const auto size = static_cast<long long>(open->Bytes->size());
    long long base = 0;
    switch (origin)
    {
    case SEEK_SET: base = 0; break;
    case SEEK_CUR: base = static_cast<long long>(open->Cursor); break;
    case SEEK_END: base = size; break;
    default: return false;
    }

    const long long target = base + offset;
    // Seeking to exactly the end is legal and is how a reader asks for the
    // length; seeking past it is not, and must fail rather than clamp.
    if (target < 0 || target > size)
        return false;

    open->Cursor = static_cast<std::size_t>(target);
    return true;
}

size_t RmlPackageFileSource::Tell(Rml::FileHandle file)
{
    const OpenFile* open = Resolve(file);
    return open != nullptr ? open->Cursor : 0;
}

size_t RmlPackageFileSource::Length(Rml::FileHandle file)
{
    const OpenFile* open = Resolve(file);
    return open != nullptr && open->Bytes != nullptr ? open->Bytes->size() : 0;
}
