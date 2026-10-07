#include <core/assets/AssetRefCodec.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonValue.h>
#include <core/serialization/JsonArchive.h>

#include <gtest/gtest.h>

#include <optional>
#include <string>

// The one text form of an asset reference: a bare path, or an id with the path
// it was last known by, read the same through an archive or as JSON.
namespace
{
constexpr std::string_view kStone = "asset://materials/stone.smat";
const AssetId kStoneId{ 0x00000000000abcdeull };

JsonValue Parse(std::string_view text)
{
    std::optional<JsonValue> parsed = JsonParse(text);
    EXPECT_TRUE(parsed.has_value()) << text;
    return parsed.value_or(JsonValue{});
}

std::optional<AssetRef> ReadField(std::string_view json)
{
    const JsonValue root = Parse(json);
    JsonReadArchive archive(root);
    AssetRef ref;
    if (!ReadAssetRef(archive, "ref", ref))
        return std::nullopt;
    return ref;
}
} // namespace

TEST(AssetRefCodec, ABarePathIsAReferenceWithoutAnId)
{
    const std::optional<AssetRef> ref = AssetRefFromJson(JsonValue(std::string(kStone)));
    ASSERT_TRUE(ref.has_value());
    EXPECT_EQ(ref->Path, kStone);
    EXPECT_FALSE(ref->Id.IsValid());
}

TEST(AssetRefCodec, AnIdTravelsWithThePathOrAlone)
{
    const std::optional<AssetRef> both = AssetRefFromJson(Parse(R"({"id": "00000000000abcde", "path": "asset://materials/stone.smat"})"));
    ASSERT_TRUE(both.has_value());
    EXPECT_EQ(both->Id, kStoneId);
    EXPECT_EQ(both->Path, kStone);

    const std::optional<AssetRef> alone = AssetRefFromJson(Parse(R"({"id": "00000000000abcde"})"));
    ASSERT_TRUE(alone.has_value());
    EXPECT_EQ(alone->Id, kStoneId);
    EXPECT_TRUE(alone->Path.empty());
}

TEST(AssetRefCodec, AnythingElseIsRefused)
{
    for (const char* text : { R"("materials/stone.smat")", R"("")", R"({})", R"({"path": ""})",
                              R"({"id": "ABCDE", "path": "asset://materials/stone.smat"})", R"({"id": 7})", "42" })
        EXPECT_FALSE(AssetRefFromJson(Parse(text)).has_value()) << text;
}

TEST(AssetRefCodec, WritingChoosesTheObjectOnlyWhenTheIdIsKnown)
{
    EXPECT_TRUE(AssetRefToJson(AssetRef{ AssetType::Material, std::string(kStone) }).IsString());
    const JsonValue stamped = AssetRefToJson(AssetRef{ AssetType::Material, std::string(kStone), kStoneId });
    ASSERT_TRUE(stamped.IsObject());
    EXPECT_EQ(AssetRefFromJson(stamped)->Id, kStoneId);
    EXPECT_EQ(AssetRefFromJson(stamped)->Path, kStone);
}

TEST(AssetRefCodec, AnArchiveReadsAndWritesTheSameForm)
{
    JsonWriteArchive writer;
    writer.BeginObject("");
    WriteAssetRef(writer, "ref", AssetRef{ AssetType::Material, std::string(kStone), kStoneId });
    writer.End();
    const JsonValue written = writer.TakeValue();
    ASSERT_NE(written.Find("ref"), nullptr);
    EXPECT_EQ(AssetRefFromJson(*written.Find("ref"))->Id, kStoneId);

    JsonReadArchive reader(written);
    AssetRef read;
    ASSERT_TRUE(ReadAssetRef(reader, "ref", read));
    EXPECT_EQ(read.Id, kStoneId);
    EXPECT_EQ(read.Path, kStone);

    EXPECT_EQ(ReadField(R"({"ref": "asset://materials/stone.smat"})")->Path, kStone);
    EXPECT_FALSE(ReadField(R"({"ref": {"id": "zz"}})").has_value());
    EXPECT_FALSE(ReadField(R"({"ref": 3})").has_value());
}

TEST(AssetIdMinting, AnAssetIsBornWithTheIdOfItsFirstPath)
{
    EXPECT_EQ(AssetIdOfPath(kStone), AssetIdOfPath(kStone));
    EXPECT_TRUE(AssetIdOfPath(kStone).IsValid());
    EXPECT_NE(AssetIdOfPath(kStone), AssetIdOfPath("asset://materials/brick.smat"));
    EXPECT_NE(AssetIdOfPath(kStone), AssetIdOfPath(kStone, 1)) << "a salt mints another for a taken id";
}

TEST(AssetIdMinting, APartIsNamedByItsFileAndItsName)
{
    const AssetId hero = AssetIdOfPath("asset://meshes/hero.blend");
    const AssetId other = AssetIdOfPath("asset://meshes/villain.blend");
    EXPECT_EQ(AssetIdOfPart(hero, "anim:Walk"), AssetIdOfPart(hero, "anim:Walk"));
    EXPECT_NE(AssetIdOfPart(hero, "anim:Walk"), AssetIdOfPart(hero, "anim:Run"));
    EXPECT_NE(AssetIdOfPart(hero, "anim:Walk"), AssetIdOfPart(other, "anim:Walk"));
    EXPECT_NE(AssetIdOfPart(hero, "anim:Walk"), hero);
}
