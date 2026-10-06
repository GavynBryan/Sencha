#include <ui/UiScreenDesc.h>

#include <gtest/gtest.h>

// A document reads `row.control` as a name; every control has one, and the
// name reads back as the same control.
TEST(UiRowControl, EveryControlsNameReadsBackAsIt)
{
    for (const UiRowControl control :
         { UiRowControl::Text, UiRowControl::Range, UiRowControl::Choice, UiRowControl::Toggle, UiRowControl::Asset,
           UiRowControl::Color, UiRowControl::Section, UiRowControl::Action, UiRowControl::LongText,
           UiRowControl::Number, UiRowControl::Vector })
    {
        const std::optional<UiRowControl> read = ParseUiRowControl(UiRowControlName(control));
        ASSERT_TRUE(read.has_value()) << UiRowControlName(control);
        EXPECT_EQ(*read, control);
    }
    EXPECT_FALSE(ParseUiRowControl("dial").has_value());
}
