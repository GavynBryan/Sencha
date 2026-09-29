#include "input/InputProfilePreview.h"

#include "data/DataDocument.h"
#include "data/DataDocumentSet.h"
#include "JsonObjectEdit.h"

#include <optional>

void InputProfilePreview::Update(const DataDocument& document,
                                 const DataDocumentSet& documents)
{
    const JsonValue* data = document.Data();
    const std::string referenced =
        data != nullptr ? ReadMemberString(*data, "actions") : std::string{};

    // The action set's own revision counts when it is open: adding an action in
    // its tab must reach this profile's pickers without a save.
    std::uint64_t actionSetRevision = 0;
    if (const DataDocument* open = documents.Find(referenced))
        actionSetRevision = open->Revision();

    const bool sameSource = HasSource
        && SourcePath == document.VirtualPath()
        && SourceRevision == document.Revision()
        && ActionSetRevision == actionSetRevision;
    if (sameSource)
        return;

    HasSource = true;
    SourcePath = document.VirtualPath();
    SourceRevision = document.Revision();
    ActionSetRevision = actionSetRevision;
    Rebuild(document, documents);
}

void InputProfilePreview::Rebuild(const DataDocument& document,
                                  const DataDocumentSet& documents)
{
    Actions.clear();
    Unbound.clear();
    LoadError.clear();
    ReferencedPath.clear();

    const JsonValue* data = document.Data();
    if (data == nullptr)
        return;

    ReferencedPath = ReadMemberString(*data, "actions");
    if (ReferencedPath.empty())
    {
        LoadError = "This profile does not say which action set it binds against.";
        return;
    }

    const std::optional<JsonValue> actionSet = documents.CurrentRoot(ReferencedPath);
    if (!actionSet)
    {
        LoadError = "No action set at '" + ReferencedPath + "'.";
        return;
    }
    const JsonValue* actionSetRoot = &*actionSet;

    if (ReadMemberString(*actionSetRoot, "type") != "input.actions")
    {
        LoadError = "'" + ReferencedPath + "' is not an input.actions asset.";
        return;
    }

    const JsonValue* actionSetData = FindMember(*actionSetRoot, "data");
    if (actionSetData == nullptr)
    {
        LoadError = "'" + ReferencedPath + "' has no data.";
        return;
    }

    Actions = CollectInputActions(*actionSetData);
    Unbound = UnboundInputActions(Actions, *data);
}
