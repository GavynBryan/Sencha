#include "authoring/AnimationClipEventsDocument.h"

#include <core/io/FileBytes.h>

#include <algorithm>
#include <format>
#include <fstream>
#include <utility>

struct AnimationClipEventsSnapshot final : ICommand
{
    AnimationClipEventsDocument& Document;
    std::vector<AnimationClipEvent> Before;
    std::vector<AnimationClipEvent> After;

    AnimationClipEventsSnapshot(AnimationClipEventsDocument& document, std::vector<AnimationClipEvent> before,
                                std::vector<AnimationClipEvent> after)
        : Document(document)
        , Before(std::move(before))
        , After(std::move(after))
    {
    }

    void Execute() override { Document.Set(After); }
    void Undo() override { Document.Set(Before); }
};

namespace
{
    // Two event lists are equal exactly when they serialize to the same sidecar text.
    std::string SidecarText(const std::string& clip, const std::vector<AnimationClipEvent>& events)
    {
        MeshImportSettings settings;
        settings.Clips[clip].Events = events;
        return WriteMeshImportSettings(settings);
    }
}

std::unique_ptr<AnimationClipEventsDocument> AnimationClipEventsDocument::Open(std::string clipPath,
                                                                               std::filesystem::path sidecarFile,
                                                                               std::string* error)
{
    const std::optional<MeshClipSource> source = MeshClipSourceOf(clipPath);
    if (!source)
    {
        if (error != nullptr)
            *error = std::format("'{}' is not a clip cooked from a mesh source.", clipPath);
        return nullptr;
    }

    std::unique_ptr<AnimationClipEventsDocument> document(new AnimationClipEventsDocument());
    document->Clip = std::move(clipPath);
    document->Name = source->ClipName;
    document->Sidecar = std::move(sidecarFile);

    if (!document->ReadSidecar(document->Settings, error))
        return nullptr;
    document->Baseline.Record(document->Sidecar);
    if (const auto it = document->Settings.Clips.find(document->Name); it != document->Settings.Clips.end())
        document->Working = it->second.Events;
    document->Saved = document->Working;
    return document;
}

const AnimationClipEvent* AnimationClipEventsDocument::Find(std::uint32_t key) const
{
    const auto it = std::ranges::find(Working, key, &AnimationClipEvent::Key);
    return it != Working.end() ? &*it : nullptr;
}

std::vector<AnimationClipEvent> AnimationClipEventsDocument::CookedOrder() const
{
    std::vector<AnimationClipEvent> events = Working;
    std::ranges::sort(events, [](const AnimationClipEvent& a, const AnimationClipEvent& b) {
        return a.Time != b.Time ? a.Time < b.Time : a.Key < b.Key;
    });
    return events;
}

std::vector<std::string> AnimationClipEventsDocument::Problems() const
{
    std::vector<std::string> problems;
    for (const AnimationClipEvent& event : Working)
    {
        std::string why;
        if (!ValidateAnimationClipEvent(event, &why))
            problems.push_back(std::move(why));
    }
    return problems;
}

bool AnimationClipEventsDocument::IsDirty() const
{
    return SidecarText(Name, Working) != SidecarText(Name, Saved);
}

void AnimationClipEventsDocument::Set(std::vector<AnimationClipEvent> events)
{
    Working = std::move(events);
    ++ContentRevision;
}

std::uint32_t AnimationClipEventsDocument::Add(AnimationClipEvent event)
{
    CancelEdit();
    if (event.Key == 0 || Find(event.Key) != nullptr)
    {
        std::uint32_t next = 1;
        for (const AnimationClipEvent& existing : Working)
            next = std::max(next, existing.Key + 1);
        event.Key = next;
    }
    const std::uint32_t key = event.Key;
    std::vector<AnimationClipEvent> after = Working;
    after.push_back(std::move(event));
    History.Execute(std::make_unique<AnimationClipEventsSnapshot>(*this, Working, std::move(after)));
    return key;
}

void AnimationClipEventsDocument::Remove(std::uint32_t key)
{
    CancelEdit();
    std::vector<AnimationClipEvent> after = Working;
    std::erase_if(after, [key](const AnimationClipEvent& event) { return event.Key == key; });
    if (after.size() != Working.size())
        History.Execute(std::make_unique<AnimationClipEventsSnapshot>(*this, Working, std::move(after)));
}

void AnimationClipEventsDocument::Replace(AnimationClipEvent event)
{
    CancelEdit();
    std::vector<AnimationClipEvent> after = Working;
    const auto it = std::ranges::find(after, event.Key, &AnimationClipEvent::Key);
    if (it == after.end())
        return;
    *it = std::move(event);
    if (SidecarText(Name, after) != SidecarText(Name, Working))
        History.Execute(std::make_unique<AnimationClipEventsSnapshot>(*this, Working, std::move(after)));
}

void AnimationClipEventsDocument::BeginEdit(std::uint32_t key)
{
    if (Editing && Editing->Key == key)
        return;
    CommitEdit();
    if (Find(key) == nullptr)
        return;
    Editing = OpenEdit{ key, Working };
    // Undo, or a newer pending edit elsewhere, cancels this edit first.
    History.OpenPendingEdit([this] { CancelEdit(); });
}

void AnimationClipEventsDocument::PreviewEdit(AnimationClipEvent event)
{
    if (!Editing || event.Key != Editing->Key)
        return;
    const auto it = std::ranges::find(Working, event.Key, &AnimationClipEvent::Key);
    if (it == Working.end())
        return;
    *it = std::move(event);
    ++ContentRevision;
}

void AnimationClipEventsDocument::CommitEdit()
{
    if (!Editing)
        return;
    std::vector<AnimationClipEvent> baseline = std::move(Editing->Baseline);
    Editing.reset();
    History.ClosePendingEdit();
    if (SidecarText(Name, baseline) != SidecarText(Name, Working))
        History.Execute(std::make_unique<AnimationClipEventsSnapshot>(*this, std::move(baseline), Working));
}

void AnimationClipEventsDocument::CancelEdit()
{
    if (!Editing)
        return;
    std::vector<AnimationClipEvent> baseline = std::move(Editing->Baseline);
    Editing.reset();
    History.ClosePendingEdit();
    Set(std::move(baseline));
}

std::optional<std::uint32_t> AnimationClipEventsDocument::EditingKey() const
{
    return Editing ? std::optional(Editing->Key) : std::nullopt;
}

void AnimationClipEventsDocument::Undo()
{
    if (Editing)
    {
        CancelEdit();
        return;
    }
    History.Undo();
}

void AnimationClipEventsDocument::Redo()
{
    CancelEdit();
    History.Redo();
}

bool AnimationClipEventsDocument::IsExternallyModified() const
{
    return Baseline.FileChanged(Sidecar);
}

bool AnimationClipEventsDocument::Save(std::string* error)
{
    CommitEdit();
    const auto fail = [&](std::string message) {
        if (error != nullptr)
            *error = std::move(message);
        return false;
    };
    if (IsExternallyModified())
        return fail(std::format("{} changed on disk since it was read; keep yours or take the file's.",
                                Sidecar.generic_string()));
    return Write(error);
}

bool AnimationClipEventsDocument::SaveOverFile(std::string* error)
{
    CommitEdit();
    MeshImportSettings current;
    if (!ReadSidecar(current, error))
        return false;
    Settings = std::move(current);
    return Write(error);
}

bool AnimationClipEventsDocument::AdoptFileVersion(std::string* error)
{
    CancelEdit();
    MeshImportSettings current;
    if (!ReadSidecar(current, error))
        return false;
    const auto it = current.Clips.find(Name);
    std::vector<AnimationClipEvent> events = it != current.Clips.end() ? it->second.Events
                                                                        : std::vector<AnimationClipEvent>{};
    Settings = std::move(current);
    Saved = events;
    Baseline.Record(Sidecar);
    if (SidecarText(Name, Working) != SidecarText(Name, events))
        History.Execute(std::make_unique<AnimationClipEventsSnapshot>(*this, Working, std::move(events)));
    return true;
}

bool AnimationClipEventsDocument::ReadSidecar(MeshImportSettings& settings, std::string* error) const
{
    std::error_code ec;
    if (!std::filesystem::exists(Sidecar, ec))
    {
        settings = {};
        return true;
    }
    std::vector<std::byte> bytes;
    std::string parseError;
    if (ReadFileBytes(Sidecar, bytes) && ParseMeshImportSettings(bytes, settings, &parseError))
        return true;
    if (error != nullptr)
        *error = std::format("{}: {}", Sidecar.generic_string(), parseError.empty() ? "could not be read" : parseError);
    return false;
}

bool AnimationClipEventsDocument::Write(std::string* error)
{
    const auto fail = [&](std::string message) {
        if (error != nullptr)
            *error = std::move(message);
        return false;
    };
    if (const std::vector<std::string> problems = Problems(); !problems.empty())
        return fail(problems.front());

    MeshImportSettings settings = Settings;
    settings.Clips[Name].Events = Working;
    if (settings.Clips[Name].IsDefault())
        settings.Clips.erase(Name);
    const std::string text = WriteMeshImportSettings(settings);
    {
        std::ofstream file(Sidecar, std::ios::binary | std::ios::trunc);
        file << text;
        if (!file.good())
            return fail(std::format("{} could not be written.", Sidecar.generic_string()));
    }
    Settings = std::move(settings);
    Saved = Working;
    Baseline.Record(Sidecar);
    return true;
}
