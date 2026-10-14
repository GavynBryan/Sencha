#include "thumbnails/AssetPictures.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

// One place to ask for any content file's picture: whoever can make a kind of
// picture provides it for as long as it holds the registration, the latest
// for an extension speaking for it.
namespace
{
class Source final : public IAssetPictureSource
{
public:
    explicit Source(std::string name)
        : Name(std::move(name))
    {
    }
    AssetPicture PictureOf(std::string_view assetPath) override
    {
        return { .Image = Name + ":" + std::string(assetPath) };
    }
    void Forget() override { ++Forgotten; }

    std::string Name;
    int Forgotten = 0;
};
} // namespace

TEST(AssetPictures, AFileIsPicturedByWhateverProvidesItsKind)
{
    AssetPictures pictures;
    Source textures("tex");
    Source studio("studio");
    const AssetPictures::Registration a = pictures.Provide(".png", textures);
    AssetPictures::Registration b = pictures.Provide(".sscene", studio);

    EXPECT_EQ(pictures.PictureOf("asset://t/Brick.PNG").Image, "tex:asset://t/Brick.PNG") << "the kind in any case";
    EXPECT_EQ(pictures.PictureOf("asset://levels/hub.sscene").Image, "studio:asset://levels/hub.sscene");
    EXPECT_TRUE(pictures.PictureOf("asset://data/run.sdata").Image.empty()) << "no one pictures data";

    pictures.Forget();
    EXPECT_EQ(textures.Forgotten, 1);
    EXPECT_EQ(studio.Forgotten, 1);

    b = {};
    EXPECT_TRUE(pictures.PictureOf("asset://levels/hub.sscene").Image.empty()) << "gone with its registration";
}

TEST(AssetPictures, TheLatestToProvideAKindSpeaksForIt)
{
    AssetPictures pictures;
    Source first("first");
    Source second("second");
    AssetPictures::Registration a = pictures.Provide(".smat", first);
    const AssetPictures::Registration b = pictures.Provide(".smat", second);
    EXPECT_EQ(pictures.PictureOf("asset://m.smat").Image, "second:asset://m.smat");
    a = {};
    EXPECT_EQ(pictures.PictureOf("asset://m.smat").Image, "second:asset://m.smat")
        << "the earlier one going leaves the later be";
}
