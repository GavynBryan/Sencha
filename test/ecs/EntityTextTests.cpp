#include <ecs/EntityText.h>

#include <gtest/gtest.h>

#include <string>
#include <string_view>

TEST(EntityText, ParsesWhatItPrints)
{
    const EntityId entity{ 42, 7 };
    std::string text;
    AppendEntityText(text, entity);

    const std::optional<EntityId> parsed = ParseEntityText(text);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, entity);
}

TEST(EntityText, RefusesAnythingButIndexColonGeneration)
{
    for (const std::string_view text : { "", "42", "42:", ":7", "42:7x", "x42:7", "42 :7", "-1:7",
                                         "42:7:1", "99999999999:1" })
        EXPECT_FALSE(ParseEntityText(text).has_value()) << "'" << text << "'";
}
