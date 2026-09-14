#pragma once

#include <RmlUi/Core/FileInterface.h>

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

struct UiPackage;
class Logger;

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
    const UiPackage* Active = nullptr;
    std::unordered_map<std::uint64_t, OpenFile> Files;
    // Never reused, so a handle outliving its Close reads as invalid rather
    // than as whichever file took the slot.
    std::uint64_t NextHandle = 1;
};
