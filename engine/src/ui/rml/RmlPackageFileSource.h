#pragma once

#include <RmlUi/Core/FileInterface.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct UiPackage;
class Logger;

//=============================================================================
// IUiPackageResourceBytes
//
// Bytes for a resource the package declared but does not carry: a font face,
// which is a real asset with its own identity and so is referenced rather than
// copied into the package.
//
// A document engine asks for a font through the file interface, the same way it
// asks for a stylesheet, so this is where that request lands.
//=============================================================================
class IUiPackageResourceBytes
{
public:
    virtual ~IUiPackageResourceBytes() = default;

    // False when the open screen's resource table does not name it, which is an
    // authoring error rather than a reason to look on disk.
    [[nodiscard]] virtual bool ResolveResourceBytes(std::string_view source,
                                                    const std::vector<std::byte>*& outBytes) = 0;
};

//=============================================================================
// RmlPackageFileSource
//
// Every file the document engine asks for, answered from the package being
// opened. Nothing here touches the filesystem, which is the point: a shipped
// build has no loose .rml or .rcss to find, and a package that cooked is a
// package that opens.
//
// The engine's file interface is global while a document load is not, so the
// package in scope is bound for the duration of one load. Loading is
// synchronous inside Context::LoadDocument, so the binding is a scope rather
// than a mode: ActivePackageScope sets it, the destructor clears it, and a
// request arriving with nothing bound is an error rather than a fallback to
// the last package that happened to be open.
//=============================================================================
class RmlPackageFileSource final : public Rml::FileInterface
{
public:
    explicit RmlPackageFileSource(Logger& log);

    // Consulted when the package carries no blob under the requested name.
    void SetResourceBytes(IUiPackageResourceBytes* resolver) { Resources = resolver; }

    // A file the host supplies under a name packages already reference -- a
    // theme stylesheet, in practice.
    //
    // Answered BEFORE the package's own blob, which is the whole point: the
    // package carries a copy so that it opens in a process that supplies
    // nothing, and the host replaces it when it has something better. Empty
    // bytes remove the override rather than serving an empty file, so a host
    // can put a package back on its own copy.
    //
    // False when the bytes are the ones already there, so a host that hands its
    // theme over every frame costs a comparison and restyles nothing.
    [[nodiscard]] bool ReplaceHostFile(std::string_view name, std::vector<std::byte> bytes);

    // Binds `package` for as long as it lives. Not reentrant: one load at a
    // time, which is what the document engine does anyway.
    class ActivePackageScope
    {
    public:
        ActivePackageScope(RmlPackageFileSource& source, const UiPackage& package);
        ~ActivePackageScope();

        ActivePackageScope(const ActivePackageScope&) = delete;
        ActivePackageScope& operator=(const ActivePackageScope&) = delete;

    private:
        RmlPackageFileSource& Source;
    };

    Rml::FileHandle Open(const Rml::String& path) override;
    void Close(Rml::FileHandle file) override;
    size_t Read(void* buffer, size_t size, Rml::FileHandle file) override;
    bool Seek(Rml::FileHandle file, long offset, int origin) override;
    size_t Tell(Rml::FileHandle file) override;
    size_t Length(Rml::FileHandle file) override;

private:
    struct OpenFile
    {
        const std::vector<std::byte>* Bytes = nullptr;
        std::size_t Cursor = 0;
    };

    [[nodiscard]] OpenFile* Resolve(Rml::FileHandle file);

    Logger& Log;
    IUiPackageResourceBytes* Resources = nullptr;
    std::unordered_map<std::string, std::vector<std::byte>> HostFiles;
    const UiPackage* Active = nullptr;
    std::unordered_map<std::uint64_t, OpenFile> Files;
    // Never reused, so a handle outliving its Close reads as invalid rather
    // than as whichever file took the slot.
    std::uint64_t NextHandle = 1;
};
