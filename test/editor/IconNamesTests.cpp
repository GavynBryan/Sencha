#include "icons/IconNames.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

// An icon's name is its key everywhere: the file it is drawn from, the class a
// document shows it by, and the name data refers to it by.
TEST(IconNames, EveryIconIsFoundByItsNameAndHasItsFileAndClass)
{
    const std::filesystem::path ui = std::filesystem::path(SENCHA_REPO_ROOT) / "editor" / "ui";
    std::ifstream sheet(ui / "icons.rcss");
    ASSERT_TRUE(sheet.is_open());
    std::stringstream classes;
    classes << sheet.rdbuf();

    for (int id = 1; id < static_cast<int>(IconId::Count); ++id)
    {
        const auto icon = static_cast<IconId>(id);
        const std::string name(IconName(icon));
        ASSERT_FALSE(name.empty()) << "icon " << id << " has no name";
        EXPECT_EQ(IconByName(name), icon) << name;
        EXPECT_TRUE(std::filesystem::exists(ui / "icons" / (name + ".svg"))) << name;
        EXPECT_NE(classes.str().find(".icon-" + name + " "), std::string::npos) << name;
    }
    EXPECT_EQ(IconByName(""), IconId::None);
    EXPECT_EQ(IconByName("no-such-icon"), IconId::None);
}
