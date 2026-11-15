#include <sencha/testing/UiLayoutTest.h>

#include <fstream>
#include <set>
#include <string>

TEST(UiBaseStyleSheet, ItSetsElementDefaultsAndNothingVisual)
{
    std::ifstream file(std::string(SENCHA_REPO_ROOT) + "/engine/assets/ui/base.rcss", std::ios::binary);
    std::string sheet((std::istreambuf_iterator<char>(file)), {});
    ASSERT_FALSE(sheet.empty());
    for (std::size_t open = sheet.find("/*"); open != std::string::npos; open = sheet.find("/*"))
        sheet.erase(open, sheet.find("*/", open) + 2 - open);

    static const std::set<std::string> kLayoutOnly{ "display", "width", "height", "margin" };
    std::size_t properties = 0;
    for (std::size_t colon = sheet.find(':'); colon != std::string::npos; colon = sheet.find(':', colon + 1))
    {
        std::size_t start = sheet.find_last_of("{;\n", colon) + 1;
        std::string name = sheet.substr(start, colon - start);
        std::erase_if(name, [](char c) { return c == ' ' || c == '\t'; });
        EXPECT_TRUE(kLayoutOnly.contains(name)) << "the base stylesheet sets '" << name << "'";
        ++properties;
    }
    EXPECT_GT(properties, 0u);
}

TEST_F(UiLayoutTest, ADocumentInAGameRootLaysOutOnTheBaseStylesheet)
{
    ASSERT_TRUE(OpenMarkup(R"(<div id="first"/><div id="second"/><div id="fill"/>)",
                           "#first, #second { height: 20px; } #fill { height: 100%; }", RenderExtent{ 640, 360 })
                    .IsValid());
    Refresh();

    const std::optional<UiElementBox> first = BoxOf("first");
    const std::optional<UiElementBox> second = BoxOf("second");
    const std::optional<UiElementBox> fill = BoxOf("fill");
    ASSERT_TRUE(first && second && fill);
    EXPECT_FLOAT_EQ(first->Width, 640.0f) << "a div is not laid out as a block";
    EXPECT_GT(second->Y, first->Y) << "two divs did not stack";
    EXPECT_FLOAT_EQ(fill->Height, 360.0f) << "the body does not fill its surface";
    EXPECT_FLOAT_EQ(first->X, 0.0f) << "the body has a margin";
    EXPECT_TRUE(NoErrors());
}
