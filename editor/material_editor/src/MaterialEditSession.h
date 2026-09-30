#pragma once

#include "documents/FileBaseline.h"

#include <assets/material/MaterialFormat.h>

#include <cstdint>
#include <string>

// Whether two descriptions author the same material (field-wise; texture
// slots compare by path). The session's dirty state is derived from this, so
// an edit that lands back on the saved value clears the dirty flag.
[[nodiscard]] bool SameMaterialDescription(const MaterialDescription& a,
                                           const MaterialDescription& b);

//=============================================================================
// MaterialEditSession
//
// The open-material state of the material editor: which .smat is open, its
// last-saved description, and the working (edited) description. Pure data +
// file I/O through MaterialLoader/MaterialWriter, so the whole edit cycle is
// headless-testable. Version() bumps on every working-state change (including
// Open); the app layer watches it to push the working description into the
// resident material for live preview.
//=============================================================================
class MaterialEditSession
{
public:
    // Loads `filePath` and makes it the open material. On parse failure the
    // previous open material is kept and *error describes the failure.
    bool Open(std::string virtualPath, std::string filePath, std::string* error);
    void Close();

    [[nodiscard]] bool HasOpen() const { return !OpenVirtualPath.empty(); }
    [[nodiscard]] const std::string& VirtualPath() const { return OpenVirtualPath; }
    [[nodiscard]] const std::string& FilePath() const { return OpenFilePath; }

    [[nodiscard]] const MaterialDescription& Working() const { return WorkingState; }
    void SetWorking(const MaterialDescription& description);

    [[nodiscard]] const MaterialDescription& Saved() const { return SavedState; }
    [[nodiscard]] bool IsDirty() const { return Dirty; }
    [[nodiscard]] uint64_t Version() const { return StateVersion; }
    // The file changed on disk since this session last read or wrote it.
    [[nodiscard]] bool IsExternallyModified() const;

    // Writes the working description back to the open file; saved state
    // becomes the working state. Refuses a file changed on disk since.
    bool Save(std::string* error);
    // Save, over whatever the file now holds.
    bool SaveOverFile(std::string* error);
    // Takes the file's version as both saved and working state.
    bool ReloadFromFile(std::string* error);

    // Writes the working description to another file (duplicate). Does not
    // change which material is open.
    bool SaveTo(const std::string& filePath, std::string* error) const;

    // Writes a default-constructed material to `filePath` (new material).
    static bool CreateNew(const std::string& filePath, std::string* error);

    // Re-points the open material after its file moved on disk (rename/move).
    // Saved and working state are unaffected; the content did not change.
    void RenameTo(std::string virtualPath, std::string filePath);

private:
    std::string OpenVirtualPath;
    std::string OpenFilePath;
    MaterialDescription SavedState;
    MaterialDescription WorkingState;
    FileBaseline Baseline;
    bool Dirty = false;
    uint64_t StateVersion = 0;
};
