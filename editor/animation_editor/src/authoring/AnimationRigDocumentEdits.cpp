#include "authoring/AnimationRigDocumentEdits.h"

#include "authoring/AnimationEventBindings.h"
#include "authoring/AnimationNameDeclarations.h"
#include "authoring/AnimationPreviewSession.h"
#include "data/DataDocumentSet.h"
#include "ui/DataForm.h"

#include <authored/VerbRegistry.h>
#include <gameplay_tags/GameplayTagDeclarations.h>

#include <deque>
#include <format>

bool EditAnimationRig(DataDocumentSet& documents, const std::string& rigPath,
                      const std::function<bool(JsonValue&)>& edit, std::string& error)
{
    DataDocument* rig = documents.Find(rigPath);
    if (rig == nullptr)
        rig = documents.OpenOrFocus(rigPath, error);
    if (rig == nullptr)
        return false;
    JsonValue root = rig->CopyRoot();
    if (!edit(root))
        return false;
    rig->BeginEdit();
    rig->PreviewRoot(std::move(root));
    documents.Store().CommitEdit(*rig);
    return true;
}

bool CreateAnimationBinding(DataDocumentSet& documents, const AnimationPreviewSession& simulation,
                            const std::string& bindingsPath, const std::string& key, const std::string& verb,
                            std::string& error)
{
    const VerbRegistry* verbs = simulation.Verbs();
    const VerbDefinition* definition = verbs != nullptr ? verbs->Get(verbs->Find(verb)) : nullptr;
    if (definition == nullptr)
    {
        error = std::format("'{}' is not a verb the preview declares.", verb);
        return false;
    }
    DataDocument* document = documents.OpenOrFocus(bindingsPath, error);
    if (document == nullptr)
        return false;
    JsonValue root = document->CopyRoot();
    if (!AddAnimationBindingRecord(root, MakeAnimationBindingRecord(key, *definition)))
    {
        error = std::format("'{}' already declares a binding '{}'.", bindingsPath, key);
        return false;
    }
    document->ReplaceRoot(std::move(root));
    documents.Store().Changed(*document);
    return true;
}

std::vector<std::string> UndeclaredAnimationNamesOf(const AnimationPreviewSession& simulation,
                                                    const DataDocumentSet& documents)
{
    const GameplayTagRegistry* tags = simulation.Tags();
    if (tags == nullptr)
        return {};
    std::deque<JsonValue> read;
    return UndeclaredAnimationNames(simulation.Problems(), *tags, [&](std::string_view path) -> AnimationDocumentView {
        const JsonValue* root = nullptr;
        if (std::optional<JsonValue> current = documents.Store().CurrentRoot(path))
            root = &read.emplace_back(std::move(*current));
        const JsonValue* type = root != nullptr ? root->Find("type") : nullptr;
        const DataSchema* schema = type != nullptr && type->IsString() ? documents.Store().SchemaOf(type->AsString()) : nullptr;
        return { root, schema != nullptr ? &schema->Root : nullptr };
    });
}

bool DeclareUndeclaredAnimationNames(DataDocumentSet& documents, const AnimationPreviewSession& simulation,
                                     const std::string& rigPath, std::string& error)
{
    const std::vector<std::string> names = UndeclaredAnimationNamesOf(simulation, documents);
    if (names.empty() || rigPath.empty())
        return true;
    std::string relative = rigPath.substr(std::string_view("asset://").size());
    const std::size_t suffix = relative.ends_with(".rig.sdata") ? relative.size() - std::string_view(".rig.sdata").size()
                                                                : relative.size() - std::string_view(".sdata").size();
    relative = relative.substr(0, suffix) + ".tags.sdata";
    const std::string path = "asset://" + relative;

    const std::size_t active = documents.ActiveIndex();
    DataDocument* document = documents.Find(path);
    if (document == nullptr)
        document = documents.Store().IsRegistered(path) ? documents.OpenOrFocus(path, error)
                                                : documents.Create(kGameplayTagDeclarationsType, relative, error);
    if (document == nullptr)
        return false;
    JsonValue root = document->CopyRoot();
    if (AddAnimationTagDeclarations(root, names))
        ApplyFieldEdit(*document, documents, FieldEdit::Instant(), std::move(root));
    documents.SetActive(active);
    return true;
}
