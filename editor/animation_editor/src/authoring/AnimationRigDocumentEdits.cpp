#include "authoring/AnimationRigDocumentEdits.h"

#include "authoring/AnimationEventBindings.h"
#include "authoring/AnimationNameDeclarations.h"
#include "authoring/AnimationPreviewSession.h"
#include "authoring/AnimationRigEdits.h"
#include "data/DataDocumentSet.h"

#include <core/assets/AssetPath.h>
#include <anim/AnimRigData.h>
#include <assets/data/DataAssetCache.h>
#include <authored/VerbRegistry.h>
#include <gameplay_tags/GameplayTagDeclarations.h>

#include <deque>
#include <format>

bool EditAnimationRig(DataDocumentSet& documents, const std::string& rigPath,
                      const std::function<bool(JsonValue&)>& edit, std::string& error)
{
    error.clear();
    DataDocument* rig = documents.Find(rigPath);
    if (rig == nullptr)
        rig = documents.OpenOrFocus(rigPath, error);
    if (rig == nullptr)
        return false;
    JsonValue root = rig->CopyRoot();
    if (!edit(root))
        return false;
    documents.ApplyEdit(*rig, std::move(root), DataEdit::Step);
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
    std::string relative(ContentRelativePath(rigPath));
    const std::size_t suffix = relative.ends_with(".rig.sdata") ? relative.size() - std::string_view(".rig.sdata").size()
                                                                : relative.size() - std::string_view(".sdata").size();
    relative = relative.substr(0, suffix) + ".tags.sdata";
    const std::string path = AssetPathOf(relative);

    const std::size_t active = documents.ActiveIndex();
    DataDocument* document = documents.Find(path);
    if (document == nullptr)
        document = documents.Store().IsRegistered(path) ? documents.OpenOrFocus(path, error)
                                                : documents.Create(kGameplayTagDeclarationsType, relative, error);
    if (document == nullptr)
        return false;
    JsonValue root = document->CopyRoot();
    if (AddAnimationTagDeclarations(root, names))
        documents.ApplyEdit(*document, std::move(root), DataEdit::Step);
    documents.SetActive(active);
    return true;
}

std::vector<AnimMaskOp> AnimationMaskSteps(DataDocumentSet& documents, const DataAssetCache& data,
                                           const std::string& rigPath, std::size_t layer)
{
    std::vector<AnimMaskOp> steps;
    if (const DataDocument* document = documents.Find(rigPath))
    {
        JsonValue root = document->CopyRoot();
        const JsonValue::Array* layers = AnimRigLayers(root);
        const JsonValue* mask = layers != nullptr && layer < layers->size() ? (*layers)[layer].Find("mask") : nullptr;
        if (mask != nullptr && mask->IsArray())
            for (const JsonValue& entry : mask->AsArray())
            {
                AnimMaskOp op;
                if (const JsonValue* joint = entry.Find("joint"); joint != nullptr && joint->IsString())
                    op.Joint = joint->AsString();
                if (const JsonValue* exclude = entry.Find("exclude"); exclude != nullptr && exclude->IsBool())
                    op.Exclude = exclude->AsBool();
                if (const JsonValue* subtree = entry.Find("subtree"); subtree != nullptr && subtree->IsBool())
                    op.Subtree = subtree->AsBool();
                steps.push_back(std::move(op));
            }
        return steps;
    }
    if (const AnimRigData* rig = data.TryGet<AnimRigData>(data.Find(rigPath), kAnimRigType);
        rig != nullptr && layer < rig->Layers.size())
        steps = rig->Layers[layer].Mask;
    return steps;
}
