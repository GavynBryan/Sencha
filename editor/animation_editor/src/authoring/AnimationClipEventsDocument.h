#pragma once

#include "commands/CommandStack.h"
#include "documents/FileBaseline.h"

#include <assets/cook/MeshImportSettings.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// One clip's events in its mesh source's import sidecar. Other clips' entries
// in the sidecar are written back as read.
class AnimationClipEventsDocument
{
public:
    // `clipPath` is "asset://<source>#anim:<clip>". A missing sidecar opens
    // empty; an unparsable one opens nothing, since saving would destroy it.
    static std::unique_ptr<AnimationClipEventsDocument> Open(std::string clipPath, std::filesystem::path sidecarFile,
                                                             std::string* error = nullptr);

    [[nodiscard]] const std::string& ClipPath() const { return Clip; }
    [[nodiscard]] const std::string& ClipName() const { return Name; }
    [[nodiscard]] const std::filesystem::path& SidecarPath() const { return Sidecar; }

    // In authored order.
    [[nodiscard]] const std::vector<AnimationClipEvent>& Events() const { return Working; }
    [[nodiscard]] const AnimationClipEvent* Find(std::uint32_t key) const;
    // By time, then key, as the cook writes them.
    [[nodiscard]] std::vector<AnimationClipEvent> CookedOrder() const;
    [[nodiscard]] std::vector<std::string> Problems() const;
    [[nodiscard]] bool IsValid() const { return Problems().empty(); }

    // Assigns the next unused key when the event has none.
    std::uint32_t Add(AnimationClipEvent event);
    void Remove(std::uint32_t key);
    void Replace(AnimationClipEvent event);

    // Previews apply immediately; commit or cancel is one undo step.
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
    void ObserveSteps(std::function<void()> observer) { History.SetExecuteObserver(std::move(observer)); }

    // Changes on every edit, previews included.
    [[nodiscard]] std::uint64_t Revision() const { return ContentRevision; }
    [[nodiscard]] bool IsDirty() const;
    [[nodiscard]] bool IsExternallyModified() const;

    // Refuses an invalid event or a sidecar changed on disk since it was read.
    [[nodiscard]] bool Save(std::string* error = nullptr);
    // SaveOverFile re-reads the sidecar so other clips' changes there survive;
    // AdoptFileVersion takes this clip's events from the file as one undo step.
    [[nodiscard]] bool SaveOverFile(std::string* error = nullptr);
    [[nodiscard]] bool AdoptFileVersion(std::string* error = nullptr);

private:
    friend struct AnimationClipEventsSnapshot;

    AnimationClipEventsDocument() = default;
    void Set(std::vector<AnimationClipEvent> events);
    [[nodiscard]] bool ReadSidecar(MeshImportSettings& settings, std::string* error) const;
    [[nodiscard]] bool Write(std::string* error);

    std::string Clip;
    std::string Name;
    std::filesystem::path Sidecar;
    MeshImportSettings Settings;
    std::vector<AnimationClipEvent> Working;
    std::vector<AnimationClipEvent> Saved;
    FileBaseline Baseline;
    CommandStack History;
    struct OpenEdit
    {
        std::uint32_t Key = 0;
        std::vector<AnimationClipEvent> Baseline;
    };
    std::optional<OpenEdit> Editing;
    std::uint64_t ContentRevision = 0;
};
