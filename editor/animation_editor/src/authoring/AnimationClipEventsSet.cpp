#include "authoring/AnimationClipEventsSet.h"

#include <assets/cook/AssetImporter.h>
#include <assets/cook/MeshImportSettings.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>

#include <format>

AnimationClipEventsSet::AnimationClipEventsSet(RuntimeAssets& assets, DocumentSourceSet& sources)
    : Assets(assets)
    , Sources(sources)
{
    Sources.AddSource(*this);
}

AnimationClipEventsSet::~AnimationClipEventsSet()
{
    Sources.RemoveSource(*this);
}

AnimationClipEventsDocument* AnimationClipEventsSet::OpenOrFocus(const std::string& clipPath, std::string& error)
{
    if (AnimationClipEventsDocument* open = Find(clipPath))
    {
        ActiveClipPath = clipPath;
        return open;
    }
    const std::optional<MeshClipSource> source = MeshClipSourceOf(clipPath);
    const AssetRecord* record = Assets.Registry.FindByPath(clipPath);
    if (!source || record == nullptr)
    {
        error = "Events are authored on a clip cooked from a mesh source in this project.";
        return nullptr;
    }
    // The clip is cooked under its content root; the sidecar sits beside the source in that root.
    std::filesystem::path root;
    for (std::filesystem::path at(record->FilePath); at.has_parent_path() && at != at.parent_path();
         at = at.parent_path())
        if (at.filename() == kCookedCacheDirName)
        {
            root = at.parent_path();
            break;
        }
    if (root.empty())
    {
        error = std::format("'{}' was not cooked into a content root, so its source cannot be found.", clipPath);
        return nullptr;
    }
    std::unique_ptr<AnimationClipEventsDocument> document = AnimationClipEventsDocument::Open(
        clipPath, root / (source->SourceRelPath + std::string(kImportSettingsSuffix)), &error);
    if (document == nullptr)
        return nullptr;
    document->ObserveSteps([this, clipPath] { Sources.Record({ this, clipPath }); });
    Open.push_back(std::move(document));
    ActiveClipPath = clipPath;
    return Open.back().get();
}

AnimationClipEventsDocument* AnimationClipEventsSet::Find(std::string_view clipPath)
{
    for (const auto& document : Open)
        if (document->ClipPath() == clipPath)
            return document.get();
    return nullptr;
}

void AnimationClipEventsSet::CommitEdit(AnimationClipEventsDocument& document)
{
    if (!document.IsEditing())
        return;
    document.CommitEdit();
    Changed(document);
}

void AnimationClipEventsSet::CancelEdit(AnimationClipEventsDocument& document)
{
    if (!document.IsEditing())
        return;
    document.CancelEdit();
    Changed(document);
}

void AnimationClipEventsSet::Changed(AnimationClipEventsDocument& document)
{
    if (document.IsEditing())
        return;
    const bool clipChanged = Push(document);
    if (Observer)
        Observer(document, clipChanged);
}

bool AnimationClipEventsSet::PushWaiting()
{
    bool changed = false;
    for (const auto& document : Open)
    {
        const ClipEventsPreviewState* state = PreviewStateOf(*document);
        if (state != nullptr && state->Status == ClipEventsPreviewStatus::ClipNotLoaded && !document->IsEditing()
            && Assets.AnimationClips.Get(Assets.AnimationClips.Find(document->ClipPath())) != nullptr)
            changed = Push(*document) || changed;
    }
    return changed;
}

const ClipEventsPreviewState* AnimationClipEventsSet::PreviewStateOf(const AnimationClipEventsDocument& document) const
{
    const auto found = States.find(&document);
    return found == States.end() ? nullptr : &found->second;
}

void AnimationClipEventsSet::AppendChangedDocuments(std::vector<DocumentRef>& out)
{
    for (const auto& document : Open)
        if (document->IsDirty() || document->IsEditing())
            out.push_back(RefOf(*document));
}

DocumentSaveResult AnimationClipEventsSet::SaveDocument(std::string_view key)
{
    AnimationClipEventsDocument* document = Find(key);
    if (document == nullptr)
        return { {}, DocumentSaveStatus::Failed, "That document is not open." };
    CommitEdit(*document);
    std::string error;
    if (document->Save(&error))
        return { {}, DocumentSaveStatus::Saved, {} };
    return { {}, document->IsExternallyModified() ? DocumentSaveStatus::Conflict : DocumentSaveStatus::Failed,
             std::move(error) };
}

bool AnimationClipEventsSet::SettleDocument(std::string_view key, ConflictChoice choice, std::string& error)
{
    AnimationClipEventsDocument* document = Find(key);
    if (document == nullptr)
    {
        error = "That document is not open.";
        return false;
    }
    if (choice == ConflictChoice::KeepMine)
        return document->SaveOverFile(&error);
    if (!document->AdoptFileVersion(&error))
        return false;
    Changed(*document);
    return true;
}

void AnimationClipEventsSet::StepDocument(std::string_view key, DocumentStep step)
{
    AnimationClipEventsDocument* document = Find(key);
    if (document == nullptr)
        return;
    step == DocumentStep::Undo ? document->Undo() : document->Redo();
    Changed(*document);
    ActiveClipPath = std::string(key);
}

void AnimationClipEventsSet::CancelDocumentEdits()
{
    for (const auto& document : Open)
        CancelEdit(*document);
}

bool AnimationClipEventsSet::Push(AnimationClipEventsDocument& document)
{
    ClipEventsPreviewState& state = States[&document];
    const AnimationClipHandle clip = Assets.AnimationClips.Find(document.ClipPath());
    const AnimationClipData* current = Assets.AnimationClips.Get(clip);
    if (current == nullptr)
    {
        state = { ClipEventsPreviewStatus::ClipNotLoaded, {} };
        return false;
    }
    if (const std::vector<std::string> problems = document.Problems(); !problems.empty())
    {
        state = { ClipEventsPreviewStatus::KeptLastValid, problems.front() };
        return false;
    }
    AnimationClipData working = *current;
    working.Events = document.CookedOrder();
    if (!Assets.AnimationClips.ReloadInPlace(clip, std::move(working)))
    {
        state = { ClipEventsPreviewStatus::Refused, {} };
        return false;
    }
    state = { ClipEventsPreviewStatus::Current, {} };
    return true;
}
