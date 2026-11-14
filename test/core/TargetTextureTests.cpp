#include <render/TargetTextures.h>
#include <render/ui/UiDrawFrame.h>

#include <gtest/gtest.h>

#include <memory>

// Target textures: a material slot holds an encoded id and the forward pass
// resolves it per draw to whatever the writer published this frame.

TEST(TargetTextureRef, OnlyTheSchemeWithANameIsATarget)
{
    EXPECT_EQ(ParseTargetTextureRef("target:ctx/diagnostic"), "ctx/diagnostic");
    EXPECT_FALSE(ParseTargetTextureRef("target:").has_value());
    EXPECT_FALSE(ParseTargetTextureRef("asset://textures/ctx/screen.png").has_value());
    EXPECT_FALSE(ParseTargetTextureRef("").has_value());
}

TEST(TargetTextureTable, InterningANameTwiceGivesOneId)
{
    TargetTextureTable table;
    const TargetTextureId a = table.Intern("ctx/diagnostic");
    EXPECT_TRUE(a.IsValid());
    EXPECT_EQ(table.Intern("ctx/diagnostic"), a);
    EXPECT_NE(table.Intern("terminal/door"), a);
    EXPECT_EQ(table.Find("ctx/diagnostic"), a);
    EXPECT_FALSE(table.Find("nothing").IsValid());
    EXPECT_FALSE(table.Intern("").IsValid());
    EXPECT_EQ(table.NameOf(a), "ctx/diagnostic");
}

TEST(TargetTextureTable, AnOrdinarySlotPassesThrough)
{
    TargetTextureTable table;
    table.SetUnpublishedIndex(7);
    EXPECT_EQ(table.Resolve(3), 3u);
    EXPECT_EQ(table.Resolve(1023), 1023u);
    // "No texture" keeps its neutral default; it is not a target.
    EXPECT_EQ(table.Resolve(TargetTextureTable::kNoTexture), TargetTextureTable::kNoTexture);
}

TEST(TargetTextureTable, AnUnwrittenTargetSamplesTheUnpublishedTexture)
{
    TargetTextureTable table;
    table.SetUnpublishedIndex(7);
    const std::uint32_t slot = TargetTextureTable::EncodeSlot(table.Intern("ctx/diagnostic"));
    EXPECT_TRUE(TargetTextureTable::IsTargetSlot(slot));
    EXPECT_EQ(table.Resolve(slot), 7u);
}

TEST(TargetTextureTable, APublishedTargetSamplesItsWritersImageUntilWithdrawn)
{
    TargetTextureTable table;
    table.SetUnpublishedIndex(7);
    const TargetTextureId id = table.Intern("ctx/diagnostic");
    const std::uint32_t slot = TargetTextureTable::EncodeSlot(id);

    table.Publish(id, 41);
    EXPECT_TRUE(table.IsPublished(id));
    EXPECT_EQ(table.Resolve(slot), 41u);
    // A rebuilt target publishes its new index; the material is untouched.
    table.Publish(id, 52);
    EXPECT_EQ(table.Resolve(slot), 52u);

    table.Withdraw(id);
    EXPECT_FALSE(table.IsPublished(id));
    EXPECT_EQ(table.Resolve(slot), 7u);
}

TEST(TargetTextureTable, ASlotNamingNoEntrySamplesTheUnpublishedTexture)
{
    TargetTextureTable table;
    table.SetUnpublishedIndex(7);
    EXPECT_EQ(table.Resolve(TargetTextureTable::kSlotFlag | 9u), 7u);
    EXPECT_EQ(table.Resolve(TargetTextureTable::kSlotFlag), 7u);
}

namespace
{
UiDrawFrame RecordingOf(std::initializer_list<UiGeometryRef> geometry)
{
    UiDrawFrame frame;
    frame.Surface = RenderExtent{ 256, 128 };
    for (const UiGeometryRef& blob : geometry)
    {
        UiDrawCommand command;
        command.Geometry = blob;
        frame.Commands.push_back(command);
    }
    return frame;
}
} // namespace

TEST(SameUiRecording, TheSameGeometryAtTheSameSizeIsTheSame)
{
    const auto a = std::make_shared<const UiGeometryBlob>();
    const auto b = std::make_shared<const UiGeometryBlob>();
    EXPECT_TRUE(SameUiRecording(RecordingOf({ a, b }), RecordingOf({ a, b })));
    EXPECT_TRUE(SameUiRecording(RecordingOf({}), RecordingOf({})));
}

TEST(SameUiRecording, ChangedGeometryOrderOrCountDiffers)
{
    const auto a = std::make_shared<const UiGeometryBlob>();
    const auto b = std::make_shared<const UiGeometryBlob>();
    const auto c = std::make_shared<const UiGeometryBlob>();
    EXPECT_FALSE(SameUiRecording(RecordingOf({ a, b }), RecordingOf({ a, c })));
    EXPECT_FALSE(SameUiRecording(RecordingOf({ a, b }), RecordingOf({ b, a })));
    EXPECT_FALSE(SameUiRecording(RecordingOf({ a, b }), RecordingOf({ a })));
}

TEST(SameUiRecording, PlacementClipTextureAndSizeAllCount)
{
    const auto a = std::make_shared<const UiGeometryBlob>();
    const UiDrawFrame base = RecordingOf({ a });

    UiDrawFrame moved = base;
    moved.Commands[0].Translation = Vec2d{ 0.f, 1.f };
    EXPECT_FALSE(SameUiRecording(base, moved));

    UiDrawFrame clipped = base;
    clipped.Commands[0].Clip = { .Enabled = true, .X = 0, .Y = 0, .Width = 10, .Height = 10 };
    EXPECT_FALSE(SameUiRecording(base, clipped));

    UiDrawFrame textured = base;
    textured.Commands[0].Texture = { .Kind = UiTextureKind::Generated, .Generated = 3 };
    EXPECT_FALSE(SameUiRecording(base, textured));

    UiDrawFrame resized = base;
    resized.Surface = RenderExtent{ 512, 256 };
    EXPECT_FALSE(SameUiRecording(base, resized));

    UiDrawFrame transformed = base;
    transformed.Commands[0].Transform = Mat4::Identity();
    EXPECT_FALSE(SameUiRecording(base, transformed));
}

TEST(SameUiRecording, ARecordingNamingAHostImageIsNeverTheSame)
{
    // Its pixels are whatever the host published this frame.
    const auto a = std::make_shared<const UiGeometryBlob>();
    UiDrawFrame frame = RecordingOf({ a });
    frame.Commands[0].Texture = { .Kind = UiTextureKind::Host, .Host = 1 };
    EXPECT_TRUE(NamesHostImage(frame));
    EXPECT_FALSE(SameUiRecording(frame, frame));
}
