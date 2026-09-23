// A clip's events, edited in its source's sidecar: every committed change one
// undo step, an interaction one transaction that cancels exactly, and a save
// that keeps every other clip's events as it found them.

#include "authoring/AnimationClipEventsDocument.h"

#include <core/io/FileBytes.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace
{
    struct TempDir
    {
        std::filesystem::path Path = std::filesystem::temp_directory_path()
            / ("sencha_clip_events_" + std::to_string(std::random_device{}()));
        TempDir() { std::filesystem::create_directories(Path); }
        ~TempDir()
        {
            std::error_code ec;
            std::filesystem::remove_all(Path, ec);
        }
    };

    constexpr std::string_view kClip = "asset://chars/hero.blend#anim:Walk";

    void Write(const std::filesystem::path& file, std::string_view text)
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out << text;
    }

    std::string Read(const std::filesystem::path& file)
    {
        std::vector<std::byte> bytes;
        EXPECT_TRUE(ReadFileBytes(file, bytes));
        return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }

    AnimationClipEvent Step(float time)
    {
        AnimationClipEvent event;
        event.Time = time;
        event.Binding = "anim.footstep";
        return event;
    }
}

TEST(AnimationClipEventsDocument, EachChangeIsOneUndoStep)
{
    TempDir dir;
    std::string error;
    auto document = AnimationClipEventsDocument::Open(std::string(kClip), dir.Path / "hero.blend.meta", &error);
    ASSERT_NE(document, nullptr) << error;
    EXPECT_TRUE(document->Events().empty());
    EXPECT_FALSE(document->IsDirty());

    const std::uint32_t left = document->Add(Step(0.25f));
    const std::uint32_t right = document->Add(Step(0.75f));
    EXPECT_NE(left, right);
    EXPECT_TRUE(document->IsDirty());

    AnimationClipEvent renamed = *document->Find(left);
    renamed.Name = "Left foot";
    document->Replace(renamed);
    document->Remove(right);
    ASSERT_EQ(document->Events().size(), 1u);

    document->Undo(); // remove
    document->Undo(); // rename
    ASSERT_EQ(document->Events().size(), 2u);
    EXPECT_TRUE(document->Find(left)->Name.empty());
    document->Undo(); // second add
    document->Undo(); // first add
    EXPECT_TRUE(document->Events().empty());
    EXPECT_FALSE(document->IsDirty());
    document->Redo();
    EXPECT_EQ(document->Events().size(), 1u);
}

// A drag previews every frame and lands as one step; cancelling it restores
// the event exactly and leaves nothing on the stack.
TEST(AnimationClipEventsDocument, ADragIsOneStepAndCancelRestoresIt)
{
    TempDir dir;
    auto document = AnimationClipEventsDocument::Open(std::string(kClip), dir.Path / "hero.blend.meta");
    ASSERT_NE(document, nullptr);
    const std::uint32_t key = document->Add(Step(0.25f));
    const std::uint64_t before = document->Revision();

    document->BeginEdit(key);
    for (const float time : { 0.3f, 0.35f, 0.4f })
    {
        AnimationClipEvent moved = *document->Find(key);
        moved.Time = time;
        document->PreviewEdit(moved);
    }
    EXPECT_GT(document->Revision(), before);
    EXPECT_FLOAT_EQ(document->Find(key)->Time, 0.4f);
    document->CommitEdit();
    EXPECT_FLOAT_EQ(document->Find(key)->Time, 0.4f);
    document->Undo();
    EXPECT_FLOAT_EQ(document->Find(key)->Time, 0.25f) << "the whole drag is one step";

    document->BeginEdit(key);
    AnimationClipEvent moved = *document->Find(key);
    moved.Time = 0.9f;
    moved.MinWeight = 0.2f;
    document->PreviewEdit(moved);
    document->CancelEdit();
    const AnimationClipEvent& restored = *document->Find(key);
    EXPECT_FLOAT_EQ(restored.Time, 0.25f);
    EXPECT_FALSE(restored.MinWeight.has_value());
    document->Undo();
    EXPECT_TRUE(document->Events().empty()) << "the cancelled drag left no step of its own";
}

// Undo during a drag cancels the drag first; the next undo reaches what was
// committed before it.
TEST(AnimationClipEventsDocument, UndoDuringADragCancelsItFirst)
{
    TempDir dir;
    auto document = AnimationClipEventsDocument::Open(std::string(kClip), dir.Path / "hero.blend.meta");
    ASSERT_NE(document, nullptr);
    const std::uint32_t key = document->Add(Step(0.25f));
    document->BeginEdit(key);
    AnimationClipEvent moved = *document->Find(key);
    moved.Time = 0.6f;
    document->PreviewEdit(moved);

    document->Undo();
    EXPECT_FALSE(document->IsEditing());
    ASSERT_NE(document->Find(key), nullptr);
    EXPECT_FLOAT_EQ(document->Find(key)->Time, 0.25f);
    document->Undo();
    EXPECT_TRUE(document->Events().empty());
}

// Saving rewrites this clip's entry and keeps every other clip's events.
TEST(AnimationClipEventsDocument, SavingKeepsOtherClipsEvents)
{
    TempDir dir;
    const std::filesystem::path sidecar = dir.Path / "hero.blend.meta";
    Write(sidecar, R"({ "version": 1, "clips": {
        "Swing": { "events": [ { "key": 1, "time": 0.5, "binding": "melee.hit", "scope": "gameplay" } ] },
        "Walk": { "events": [ { "key": 4, "time": 0.2, "binding": "anim.footstep" } ] } } })");

    std::string error;
    auto document = AnimationClipEventsDocument::Open(std::string(kClip), sidecar, &error);
    ASSERT_NE(document, nullptr) << error;
    ASSERT_EQ(document->Events().size(), 1u);
    (void)document->Add(Step(0.7f));
    ASSERT_TRUE(document->Save(&error)) << error;
    EXPECT_FALSE(document->IsDirty());

    MeshImportSettings saved;
    const std::string text = Read(sidecar);
    ASSERT_TRUE(ParseMeshImportSettings({ reinterpret_cast<const std::byte*>(text.data()), text.size() }, saved,
                                        &error))
        << error;
    ASSERT_EQ(saved.ClipEvents.at("Swing").size(), 1u);
    EXPECT_EQ(saved.ClipEvents.at("Swing")[0].Binding, "melee.hit");
    ASSERT_EQ(saved.ClipEvents.at("Walk").size(), 2u);

    auto reopened = AnimationClipEventsDocument::Open(std::string(kClip), sidecar, &error);
    ASSERT_NE(reopened, nullptr) << error;
    EXPECT_EQ(reopened->Events().size(), 2u);
}

TEST(AnimationClipEventsDocument, SaveRefusesAnInvalidEventOrAFileChangedOnDisk)
{
    TempDir dir;
    const std::filesystem::path sidecar = dir.Path / "hero.blend.meta";
    auto document = AnimationClipEventsDocument::Open(std::string(kClip), sidecar);
    ASSERT_NE(document, nullptr);
    const std::uint32_t key = document->Add(Step(0.5f));
    AnimationClipEvent late = *document->Find(key);
    late.Time = 1.5f;
    document->Replace(late);
    EXPECT_FALSE(document->Problems().empty());
    std::string error;
    EXPECT_FALSE(document->Save(&error));
    EXPECT_NE(error.find("normalized"), std::string::npos) << error;

    late.Time = 0.5f;
    document->Replace(late);
    ASSERT_TRUE(document->Save(&error)) << error;
    std::filesystem::last_write_time(sidecar, std::filesystem::last_write_time(sidecar) + std::chrono::seconds(5));
    (void)document->Add(Step(0.1f));
    EXPECT_TRUE(document->IsExternallyModified());
    EXPECT_FALSE(document->Save(&error));
    EXPECT_NE(error.find("changed on disk"), std::string::npos) << error;
}

// A sidecar that does not parse is not opened: saving over it would lose
// whatever it held.
TEST(AnimationClipEventsDocument, AnUnreadableSidecarDoesNotOpen)
{
    TempDir dir;
    const std::filesystem::path sidecar = dir.Path / "hero.blend.meta";
    Write(sidecar, R"({ "clips": { "Walk": { "event": [] } } })");
    std::string error;
    EXPECT_EQ(AnimationClipEventsDocument::Open(std::string(kClip), sidecar, &error), nullptr);
    EXPECT_NE(error.find("'event'"), std::string::npos) << error;
    EXPECT_EQ(AnimationClipEventsDocument::Open("asset://chars/hero.blend#model:Rig", sidecar, &error), nullptr);
}
