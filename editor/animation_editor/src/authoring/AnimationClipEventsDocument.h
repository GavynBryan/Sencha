#pragma once

#include "commands/CommandStack.h"

#include <assets/cook/MeshImportSettings.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

//=============================================================================
// AnimationClipEventsDocument
//
// One clip's events, edited where they are authored: the import sidecar of the
// mesh source the clip is cooked from. A clip is rebuilt from its source on
// every import, so the sidecar is the only place an event survives; the
// document reads the whole sidecar, edits this clip's entry, and writes every
// other clip's events back as it found them.
//
// Every committed change is one undo step. A live interaction -- dragging a
// marker, typing into a field -- opens one transaction whose previews apply at
// once and whose cancel restores the event exactly as it was, and the undo
// stack's pending-edit scope makes undo during it cancel it first.
//=============================================================================
class AnimationClipEventsDocument
{
public:
    // The events of `clipPath` ("asset://<source>#anim:<clip>") from the
    // sidecar at `sidecarFile`. A missing sidecar opens empty; one that does
    // not parse opens nothing, because saving over it would lose what it held.
    static std::unique_ptr<AnimationClipEventsDocument> Open(std::string clipPath, std::filesystem::path sidecarFile,
                                                             std::string* error = nullptr);

    [[nodiscard]] const std::string& ClipPath() const { return Clip; }
    [[nodiscard]] const std::string& ClipName() const { return Name; }
    [[nodiscard]] const std::filesystem::path& SidecarPath() const { return Sidecar; }

    // In authored order.
    [[nodiscard]] const std::vector<AnimationClipEvent>& Events() const { return Working; }
    [[nodiscard]] const AnimationClipEvent* Find(std::uint32_t key) const;
    // In the order the cook writes them: by time, then key.
    [[nodiscard]] std::vector<AnimationClipEvent> CookedOrder() const;
    // One message per invalid event, naming it.
    [[nodiscard]] std::vector<std::string> Problems() const;
    [[nodiscard]] bool IsValid() const { return Problems().empty(); }

    // Adds an event, giving it the next unused key when it has none, and
    // returns its key.
    std::uint32_t Add(AnimationClipEvent event);
    void Remove(std::uint32_t key);
    // Replaces the event with the same key.
    void Replace(AnimationClipEvent event);

    // A live edit of one event: previews apply immediately and commit or
    // cancel as one step.
    void BeginEdit(std::uint32_t key);
    void PreviewEdit(AnimationClipEvent event);
    void CommitEdit();
    void CancelEdit();
    [[nodiscard]] bool IsEditing() const { return Editing.has_value(); }
    [[nodiscard]] std::optional<std::uint32_t> EditingKey() const;

    void Undo();
    void Redo();
    [[nodiscard]] bool CanUndo() const { return History.CanUndo(); }
    [[nodiscard]] bool CanRedo() const { return History.CanRedo(); }

    // Moves on every change, previews included.
    [[nodiscard]] std::uint64_t Revision() const { return ContentRevision; }
    [[nodiscard]] bool IsDirty() const;
    // The sidecar changed on disk since it was read or saved.
    [[nodiscard]] bool IsExternallyModified() const;

    // Writes the sidecar: this clip's events and every other clip's as read.
    // Refuses a file changed on disk since it was read, and an invalid event.
    [[nodiscard]] bool Save(std::string* error = nullptr);

private:
    friend struct AnimationClipEventsSnapshot;

    AnimationClipEventsDocument() = default;
    void Set(std::vector<AnimationClipEvent> events);

    std::string Clip;
    std::string Name;
    std::filesystem::path Sidecar;
    // The whole sidecar as read, for the clips this document does not edit.
    MeshImportSettings Settings;
    std::vector<AnimationClipEvent> Working;
    std::vector<AnimationClipEvent> Saved;
    std::optional<std::filesystem::file_time_type> SavedTime;
    CommandStack History;
    struct OpenEdit
    {
        std::uint32_t Key = 0;
        std::vector<AnimationClipEvent> Baseline;
    };
    std::optional<OpenEdit> Editing;
    std::uint64_t ContentRevision = 0;
};
