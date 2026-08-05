#pragma once

#include "input/InputBindingSummary.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

class DataDocument;
class DataEditorWorkspace;

// The vocabulary a profile is binding against, resolved from the action set it
// references.
//
// A profile names its actions by string, and the set declaring them is a
// separate document. Joining them here is what lets the form offer a list of
// actions instead of a text box, and say which declared actions nothing binds.
//
// An open tab wins over the file on disk: the action set is usually being
// edited in the same session, and an action added a moment ago must appear in
// the picker before it is saved.
//
// Owned by the profile's subtype editor and rebuilt only when the profile or
// the set it points at actually changes.
class InputProfilePreview
{
public:
    void Update(const DataDocument& document, const DataEditorWorkspace& workspace);

    [[nodiscard]] std::span<const KnownInputAction> KnownActions() const { return Actions; }
    [[nodiscard]] std::span<const std::string> UnboundActions() const { return Unbound; }

    // The authored reference, and why it did not resolve. Empty error means the
    // set loaded; a profile that has not named one yet reports neither.
    [[nodiscard]] const std::string& ActionSetPath() const { return ReferencedPath; }
    [[nodiscard]] const std::string& ActionSetError() const { return LoadError; }

private:
    void Rebuild(const DataDocument& document, const DataEditorWorkspace& workspace);

    std::vector<KnownInputAction> Actions;
    std::vector<std::string> Unbound;
    std::string ReferencedPath;
    std::string LoadError;

    // Cache keys: the profile being shown, and the tab supplying its actions
    // when that set is open for editing.
    std::string SourcePath;
    std::uint64_t SourceRevision = 0;
    std::uint64_t ActionSetRevision = 0;
    bool HasSource = false;
};
