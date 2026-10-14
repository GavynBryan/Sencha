#include "shell/NumberScrub.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

// A number dragged across its box is one edit, previewed live; a press let go
// in place types instead; and while it holds the pointer nothing else gets the
// keys, and whatever ends it from outside cancels it.
namespace
{
class Recording final : public IPropertySource
{
public:
    std::vector<std::string> Log;

    std::vector<PropertyNode> Nodes() const override { return {}; }
    bool BeginEdit(std::string_view key) override
    {
        Log.push_back("begin " + std::string(key));
        return true;
    }
    PropertyEditResult PreviewEdit(std::string_view, std::string_view value) override
    {
        Log.push_back("preview " + std::string(value));
        return PropertyEditResult::Applied;
    }
    PropertyEditResult CommitEdit(std::string_view, std::string_view value) override
    {
        Log.push_back("commit " + std::string(value));
        return PropertyEditResult::Applied;
    }
    void CancelEdit(std::string_view) override { Log.push_back("cancel"); }
    PropertyEditResult PreviewEditBy(std::string_view, double delta) override
    {
        Log.push_back("preview by " + std::to_string(static_cast<int>(delta * 100)));
        return PropertyEditResult::Applied;
    }
    PropertyEditResult CommitEditBy(std::string_view, double delta) override
    {
        Log.push_back("commit by " + std::to_string(static_cast<int>(delta * 100)));
        return PropertyEditResult::Applied;
    }
};

class NumberScrubTest : public ::testing::Test
{
protected:
    NumberScrub::Took Move(float pixels)
    {
        return Scrub.Take(PointerMoveEvent{ .Position = {}, .Delta = { pixels, 0.0f }, .Modifiers = {} }, Resolve);
    }
    NumberScrub::Took Release()
    {
        return Scrub.Take(PointerUpEvent{ .Position = {}, .Button = MouseButton::Left, .Modifiers = {} }, Resolve);
    }
    NumberScrub::Took Key(SDL_Keycode key) { return Scrub.Take(KeyDownEvent{ .Key = key, .Modifiers = {} }, Resolve); }

    Recording Source;
    bool WholeVector = false;
    NumberScrub::Resolve Resolve = [this](const SheetPoint&) {
        return NumberScrub::Target{ .Source = &Source, .Key = "size", .WholeVector = WholeVector };
    };
    std::vector<bool> Captured;
    NumberScrub Scrub{ [this](bool held) { Captured.push_back(held); } };
    PropertyDescriptor Bounds = [] {
        PropertyDescriptor bounds;
        bounds.Step = 1.0;
        return bounds;
    }();
};
} // namespace

TEST_F(NumberScrubTest, ADragIsOneEditPreviewedAsItGoes)
{
    Scrub.Press({ .Sheet = SheetId::Selection, .Row = 2 }, "10", {}, Bounds);
    EXPECT_EQ(Move(1.0f), NumberScrub::Took::Yes) << "within the dead zone it is still a press";
    EXPECT_TRUE(Source.Log.empty());
    EXPECT_EQ(Move(5.0f), NumberScrub::Took::Yes);
    EXPECT_EQ(Captured, (std::vector<bool>{ true }));
    ASSERT_GE(Source.Log.size(), 2u);
    EXPECT_EQ(Source.Log[0], "begin size");
    EXPECT_EQ(Release(), NumberScrub::Took::Yes);
    EXPECT_EQ(Source.Log.back().rfind("commit ", 0), 0u) << Source.Log.back();
    EXPECT_EQ(Captured, (std::vector<bool>{ true, false }));
    EXPECT_FALSE(Scrub.Holds());
}

TEST_F(NumberScrubTest, LetGoWhereItWasPressedIsAClick)
{
    Scrub.Press({ .Sheet = SheetId::Tool, .Row = 0 }, "10", {}, Bounds);
    EXPECT_EQ(Release(), NumberScrub::Took::Click);
    EXPECT_TRUE(Source.Log.empty()) << "a click edits nothing";
    EXPECT_TRUE(Captured.empty());
}

TEST_F(NumberScrubTest, NoKeyButEscapeReachesAnythingWhileItHoldsThePointer)
{
    Scrub.Press({ .Sheet = SheetId::Selection, .Row = 0 }, "10", {}, Bounds);
    (void)Move(8.0f);
    EXPECT_EQ(Key(SDLK_Z), NumberScrub::Took::Yes) << "undo mid-drag would run under the preview";
    EXPECT_EQ(Key(SDLK_DELETE), NumberScrub::Took::Yes);
    EXPECT_TRUE(Scrub.Holds());
    EXPECT_EQ(Key(SDLK_ESCAPE), NumberScrub::Took::Yes);
    EXPECT_EQ(Source.Log.back(), "cancel");
    EXPECT_FALSE(Scrub.Holds());
    EXPECT_EQ(Key(SDLK_Z), NumberScrub::Took::No) << "let go, the keys are everyone's again";
}

TEST_F(NumberScrubTest, EndedFromOutsideItCancelsAndLetsGoOfThePointer)
{
    Scrub.Press({ .Sheet = SheetId::Selection, .Row = 0 }, "10", {}, Bounds);
    (void)Move(8.0f);
    Scrub.End(false);
    EXPECT_EQ(Source.Log.back(), "cancel");
    EXPECT_EQ(Captured, (std::vector<bool>{ true, false }));
    EXPECT_EQ(Release(), NumberScrub::Took::No) << "the release after it is nothing of the scrub's";
}

TEST_F(NumberScrubTest, ANumberWithoutAKeyOfItsOwnWritesItsVectorWhole)
{
    WholeVector = true;
    Scrub.Press({ .Sheet = SheetId::Selection, .Row = 0, .Element = 1 }, "2", { "1", "2", "3" }, Bounds);
    (void)Move(8.0f);
    (void)Release();
    const std::string& commit = Source.Log.back();
    ASSERT_EQ(commit.rfind("commit 1, ", 0), 0u) << commit;
    EXPECT_TRUE(commit.ends_with(", 3")) << commit;
}

TEST_F(NumberScrubTest, ANumberStandingForSeveralMovesEachByTheAmount)
{
    Scrub.Press({ .Sheet = SheetId::Selection, .Row = 0 }, "", {}, Bounds);
    (void)Move(8.0f);
    EXPECT_EQ(Source.Log.back().rfind("preview by ", 0), 0u) << Source.Log.back();
    (void)Release();
    EXPECT_EQ(Source.Log.back().rfind("commit by ", 0), 0u) << Source.Log.back();
}
