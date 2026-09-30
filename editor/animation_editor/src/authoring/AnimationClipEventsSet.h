#pragma once

#include "authoring/AnimationClipEventsDocument.h"
#include "documents/DocumentSource.h"
#include "documents/DocumentSourceSet.h"

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct RuntimeAssets;

enum class ClipEventsPreviewStatus
{
    Current,
    ClipNotLoaded,
    KeptLastValid,
    Refused,
};

struct ClipEventsPreviewState
{
    ClipEventsPreviewStatus Status = ClipEventsPreviewStatus::ClipNotLoaded;
    std::string Problem;
};

// The open clip-event documents. Committed events replace the resident clip's,
// so every rig playing the clip fires them; previews never reach it.
class AnimationClipEventsSet final : public DocumentSource
{
public:
    using ChangeObserver = std::function<void(AnimationClipEventsDocument& document, bool clipChanged)>;

    AnimationClipEventsSet(RuntimeAssets& assets, DocumentSourceSet& sources);
    ~AnimationClipEventsSet() override;

    AnimationClipEventsSet(const AnimationClipEventsSet&) = delete;
    AnimationClipEventsSet& operator=(const AnimationClipEventsSet&) = delete;
    AnimationClipEventsSet(AnimationClipEventsSet&&) = delete;
    AnimationClipEventsSet& operator=(AnimationClipEventsSet&&) = delete;

    AnimationClipEventsDocument* OpenOrFocus(const std::string& clipPath, std::string& error);
    [[nodiscard]] AnimationClipEventsDocument* Find(std::string_view clipPath);
    [[nodiscard]] AnimationClipEventsDocument* Active() { return Find(ActiveClipPath); }
    [[nodiscard]] const std::string& ActiveClip() const { return ActiveClipPath; }
    [[nodiscard]] std::span<const std::unique_ptr<AnimationClipEventsDocument>> Documents() const { return Open; }
    [[nodiscard]] DocumentRef RefOf(const AnimationClipEventsDocument& document)
    {
        return { this, document.ClipPath() };
    }

    void CommitEdit(AnimationClipEventsDocument& document);
    void CancelEdit(AnimationClipEventsDocument& document);
    // Pushes the committed events into the resident clip and tells the observer.
    void Changed(AnimationClipEventsDocument& document);
    // True when a clip loaded since its events were edited now plays them.
    [[nodiscard]] bool PushWaiting();
    void OnChanged(ChangeObserver observer) { Observer = std::move(observer); }
    [[nodiscard]] const ClipEventsPreviewState* PreviewStateOf(const AnimationClipEventsDocument& document) const;

    void AppendChangedDocuments(std::vector<DocumentRef>& out) override;
    [[nodiscard]] DocumentSaveResult SaveDocument(std::string_view key) override;
    [[nodiscard]] bool SettleDocument(std::string_view key, ConflictChoice choice, std::string& error) override;
    void StepDocument(std::string_view key, DocumentStep step) override;
    void CancelDocumentEdits() override;
    void DiscardDocument(std::string_view key) override;

private:
    [[nodiscard]] bool Push(AnimationClipEventsDocument& document);

    RuntimeAssets& Assets;
    DocumentSourceSet& Sources;
    std::vector<std::unique_ptr<AnimationClipEventsDocument>> Open;
    std::string ActiveClipPath;
    std::unordered_map<const AnimationClipEventsDocument*, ClipEventsPreviewState> States;
    ChangeObserver Observer;
};
