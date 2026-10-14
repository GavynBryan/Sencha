#pragma once

#include "properties/PropertySource.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct CookProfile;
struct ProjectDescriptor;

//=============================================================================
// CookProfileEditor
// The project's cook profiles as edited: the selected one's settings as
// properties, duplicate, delete and save. A built-in profile is read-only until
// duplicated; edits are not undo steps, and Save writes the file.
//=============================================================================
class CookProfileEditor final : public IPropertySource
{
public:
    explicit CookProfileEditor(ProjectDescriptor& project) : Project(project) {}

    // The project's own profiles and the built-ins it inherits.
    [[nodiscard]] std::vector<CookProfile> Profiles() const;
    // Into Profiles(); a profile that has gone selects the first.
    [[nodiscard]] std::size_t Selected() const;
    void Select(std::size_t index);
    [[nodiscard]] bool SelectedIsBuiltIn() const;

    // A project-owned copy of the selected profile, selected.
    void Duplicate();
    // False for a built-in, which is not the project's to remove.
    bool Delete();
    // Status says what is wrong, or that nothing is. After an edit it says
    // only what is wrong.
    bool Validate();
    bool Save();

    [[nodiscard]] bool Changed() const { return Dirty; }
    // Moves on with every change to the profiles, saved or not.
    [[nodiscard]] std::uint64_t Revision() const { return Changes; }
    [[nodiscard]] const std::string& Status() const { return Message; }

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    bool BeginEdit(std::string_view key) override;
    PropertyEditResult PreviewEdit(std::string_view key, std::string_view value) override;
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;
    void CancelEdit(std::string_view key) override;

private:
    [[nodiscard]] CookProfile* Owned();
    // What would stop a save, or empty.
    [[nodiscard]] std::string Problem() const;
    [[nodiscard]] std::string ValueOf(std::string_view key) const;
    PropertyEditResult Set(std::string_view key, std::string_view value);

    ProjectDescriptor& Project;
    std::string SelectedId = "full";
    bool Dirty = false;
    std::uint64_t Changes = 0;
    std::string Message;
    std::string EditKey;
    std::string EditBegan;
};
