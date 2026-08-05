#include "input/InputProfilePreview.h"

#include "DataDocument.h"
#include "DataEditorWorkspace.h"
#include "JsonObjectEdit.h"

#include <core/json/JsonParser.h>

#include <fstream>
#include <optional>
#include <sstream>

namespace
{
// The open tab editing a given asset, or null. A dirty tab is the truth about
// what that asset currently declares; the file is one save behind.
const DataDocument* FindOpenDocument(const DataEditorWorkspace& workspace,
                                     std::string_view virtualPath)
{
    if (virtualPath.empty())
        return nullptr;
    for (const auto& tab : workspace.Documents())
    {
        if (tab->VirtualPath() == virtualPath)
            return tab.get();
    }
    return nullptr;
}

std::optional<JsonValue> ReadDocumentFile(const DataEditorWorkspace& workspace,
                                          std::string_view virtualPath)
{
    const std::filesystem::path file = workspace.ResolveFile(virtualPath);
    if (file.empty())
        return std::nullopt;

    std::ifstream stream(file);
    if (!stream)
        return std::nullopt;
    std::ostringstream contents;
    contents << stream.rdbuf();
    return JsonParse(contents.str());
}
}

void InputProfilePreview::Update(const DataDocument& document,
                                 const DataEditorWorkspace& workspace)
{
    const JsonValue* data = document.Data();
    const std::string referenced =
        data != nullptr ? ReadMemberString(*data, "actions") : std::string{};

    // The action set's own revision counts when it is open: adding an action in
    // its tab must reach this profile's pickers without a save.
    std::uint64_t actionSetRevision = 0;
    if (const DataDocument* open = FindOpenDocument(workspace, referenced))
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
    Rebuild(document, workspace);
}

void InputProfilePreview::Rebuild(const DataDocument& document,
                                  const DataEditorWorkspace& workspace)
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

    const JsonValue* actionSetRoot = nullptr;
    std::optional<JsonValue> loaded;
    if (const DataDocument* open = FindOpenDocument(workspace, ReferencedPath))
    {
        actionSetRoot = &open->Root();
    }
    else
    {
        loaded = ReadDocumentFile(workspace, ReferencedPath);
        if (!loaded.has_value())
        {
            LoadError = "No action set at '" + ReferencedPath + "'.";
            return;
        }
        actionSetRoot = &loaded.value();
    }

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
