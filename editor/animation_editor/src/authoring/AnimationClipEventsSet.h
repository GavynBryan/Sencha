#pragma once

#include "authoring/AnimationClipEventsDocument.h"
#include "documents/DocumentSource.h"
#include "documents/DocumentSourceSet.h"

#include <cstdint>
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

// The open clip-event documents, and every change to one: committed events
// replace the resident clip's, so every rig playing the clip fires them;
// previews never reach it.
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

    // Each one step.
    std::uint32_t Add(AnimationClipEventsDocument& document, AnimationClipEvent event);
    void Remove(AnimationClipEventsDocument& document, std::uint32_t key);
    void Replace(AnimationClipEventsDocument& document, AnimationClipEvent event);
    // Event `key` shown at `time`, 0 to 1 along the clip, as a live edit:
    // begun if none is open, reaching the clip only once committed.
    void PreviewTime(AnimationClipEventsDocument& document, std::uint32_t key, double time);
    void CommitEdit(AnimationClipEventsDocument& document);
    void CancelEdit(AnimationClipEventsDocument& document);
    // Refused, saved or discarded first as `disposition` says.
    [[nodiscard]] bool Close(std::string_view clipPath, DirtyDisposition disposition, std::string& error);
    // True when a clip loaded since its events were edited now plays them.
    [[nodiscard]] bool PushWaiting();
    void OnChanged(ChangeObserver observer) { Observer = std::move(observer); }
    [[nodiscard]] const ClipEventsPreviewState* PreviewStateOf(const AnimationClipEventsDocument& document) const;

    void AppendChangedDocuments(std::vector<DocumentRef>& out) override;
    [[nodiscard]] std::string DocumentLabel(std::string_view key) const override;
    [[nodiscard]] DocumentSaveResult SaveDocument(std::string_view key) override;
    [[nodiscard]] bool SettleDocument(std::string_view key, ConflictChoice choice, std::string& error) override;
    void StepDocument(std::string_view key, DocumentStep step) override;
    void CancelDocumentEdits() override;
    void DiscardDocument(std::string_view key) override;
    [[nodiscard]] bool HoldsFile(const std::filesystem::path& file) const override;
    void FileMoved(const MovedFile& moved) override;

private:
    // Pushes the committed events into the resident clip and tells the observer.
    void Changed(AnimationClipEventsDocument& document);
    [[nodiscard]] bool Push(AnimationClipEventsDocument& document);
    void Drop(std::string_view clipPath);

    RuntimeAssets& Assets;
    DocumentSourceSet& Sources;
    std::vector<std::unique_ptr<AnimationClipEventsDocument>> Open;
    std::string ActiveClipPath;
    std::unordered_map<const AnimationClipEventsDocument*, ClipEventsPreviewState> States;
    ChangeObserver Observer;
};
