#pragma once

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

    [[nodiscard]] bool IsDirty() const { return Dirty; }
    [[nodiscard]] uint64_t Version() const { return StateVersion; }

    // Writes the working description back to the open file; saved state
    // becomes the working state.
    bool Save(std::string* error);

    // Writes the working description to another file (duplicate). Does not
    // change which material is open.
    bool SaveTo(const std::string& filePath, std::string* error) const;

    // Writes a default-constructed material to `filePath` (new material).
    static bool CreateNew(const std::string& filePath, std::string* error);

private:
    std::string OpenVirtualPath;
    std::string OpenFilePath;
    MaterialDescription SavedState;
    MaterialDescription WorkingState;
    bool Dirty = false;
    uint64_t StateVersion = 0;
};
