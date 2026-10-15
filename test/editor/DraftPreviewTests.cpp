// The draft preview: what a previewed draft adds or changes, staged as it would
// be, and what it deletes, staged as it is, each where it stands in the world.

#include "WorkspaceFixture.h"

#include "document/EntityChangeSet.h"
#include "document/EntityNameComponent.h"
#include "operations/EditorOperations.h"
#include "operations/LevelOperations.h"
#include "operations/LevelSubjects.h"
#include "operations/LevelTarget.h"
#include "render/DraftPreview.h"

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <world/registry/Registry.h>

#include <gtest/gtest.h>

#include <set>

namespace
{
class DraftPreviewTest : public WorkspaceTest
{
protected:
    void SetUp() override
    {
        WorkspaceTest::SetUp();
        std::vector<std::string> errors;
        ASSERT_TRUE(Operations.Register("level", LevelOperations(Workspace.Level, Operations), errors));
    }

    void Ok(std::string_view name, std::string_view arguments)
    {
        const JsonValue answer = Operations.Invoke(name, JsonParse(arguments).value_or(JsonValue{}));
        ASSERT_EQ(answer.Find("status")->AsString(), "ok") << name << ": " << JsonStringify(answer);
    }

    void Show()
    {
        LevelTarget& level = Workspace.Level;
        const LevelDraft* draft = level.PreviewedDraft();
        Preview.Update(draft, draft != nullptr ? level.BaseOf(*draft) : nullptr, Scene().InteractiveEvaluationPolicy());
    }

    static std::set<std::string> Names(const DocumentStage* stage)
    {
        std::set<std::string> names;
        if (stage == nullptr)
            return names;
        const EditorScene& scene = stage->Scene();
        for (const EntityId entity : scene.GetAllEntities())
            if (const auto* name = scene.GetRegistry().Components.TryGet<EntityNameComponent>(entity))
                names.insert(std::string(name->Value.View()));
        return names;
    }

    static const Transform3f* WorldOf(const DocumentStage* stage, std::string_view name)
    {
        const SubjectResolution found = ResolveEntitySubject(stage->Document(), "@" + std::string(name));
        return found.Resolved() ? stage->Scene().TryGetWorldTransform(found.Entities.front()) : nullptr;
    }

    EditorOperations Operations;
    LoggingProvider PreviewLogging;
    DraftPreview Preview{ nullptr, PreviewLogging };
};
}

TEST_F(DraftPreviewTest, ItStagesWhatTheDraftAddsChangesAndDeletes)
{
    Ok("space.create", R"({"name": "Hall", "min": [0, 0, 0], "max": [8, 3, 6]})");
    Ok("space.create", R"({"name": "Shed", "min": [20, 0, 0], "max": [24, 3, 4]})");
    Ok("opening.create", R"({"at": [4, 1, -0.1], "name": "Door"})");

    Ok("draft.begin", R"({"label": "Rework"})");
    Ok("space.create", R"({"name": "Corridor", "min": [8.25, 0, 2], "max": [14, 3, 4]})");
    Ok("opening.move", R"({"subject": "@Door", "offset": [2, 0, 0]})");
    Ok("shell.detach", R"({"subject": "@Shed"})");
    Show();

    const std::set<std::string> added = Names(Preview.Added());
    for (const std::string_view name : { "Corridor", "Corridor north wall", "Door", "Shed", "Shed north wall" })
        EXPECT_TRUE(added.contains(std::string(name))) << name << " is staged";
    EXPECT_FALSE(added.contains("Hall")) << "the hall is unchanged; only its walls the corridor meets are";
    EXPECT_TRUE(Names(Preview.Removed()).empty()) << "the draft deletes nothing";

    const Transform3f* door = WorldOf(Preview.Added(), "Door");
    ASSERT_NE(door, nullptr);
    EXPECT_NEAR(door->Position.X, 6.0f, 1e-3f) << "the moved door stands in the world where the draft puts it, "
                                                   "though its room is not staged";
}

TEST_F(DraftPreviewTest, ADeletionIsStagedAsItWasWhereItStood)
{
    Ok("space.create", R"({"name": "Shed", "min": [20, 0, 0], "max": [24, 3, 4]})");
    Ok("draft.begin", R"({"label": "Clear the shed"})");
    const EditorOperationOutcome deleted = Workspace.Level.Mutate(
        "test.delete", JsonValue{}, [](EditorDocument& document, EntityChangeRecorder& recorder) {
            const EntityId shed = ResolveEntitySubject(document, "@Shed").Entities.front();
            recorder.Touch(shed);
            document.GetScene().DestroyEntity(shed);
            return EditorOperationOutcome::Ok({});
        });
    ASSERT_EQ(deleted.Status, EditorOperationStatus::Ok);
    Show();
    EXPECT_TRUE(Names(Preview.Added()).empty());
    EXPECT_EQ(Names(Preview.Removed()), std::set<std::string>{ "Shed" });
    const Transform3f* shed = WorldOf(Preview.Removed(), "Shed");
    ASSERT_NE(shed, nullptr);
    EXPECT_NEAR(shed->Position.X, 22.0f, 1e-3f);
}

TEST_F(DraftPreviewTest, NothingIsRebuiltWhileTheDraftStandsStillAndNothingShowsWithoutOne)
{
    Ok("draft.begin", R"({"label": "One room"})");
    Ok("space.create", R"({"name": "Hall", "min": [0, 0, 0], "max": [8, 3, 6]})");
    Show();
    const DocumentStage* first = Preview.Added();
    ASSERT_NE(first, nullptr);
    Show();
    EXPECT_EQ(Preview.Added(), first);

    Ok("draft.accept", "{}");
    Show();
    EXPECT_EQ(Preview.Added(), nullptr) << "an accepted draft is no longer previewed";
}
