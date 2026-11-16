#include <sencha/testing/UiLayoutTest.h>

#include <string>

namespace
{
class UiDataTransformTest : public UiLayoutTest
{
protected:
    void Open(double width)
    {
        UiScreenDesc desc;
        desc.ModelName = "transforms";
        desc.Properties = { UiModelProperty{ "w", UiValue(width) } };
        ASSERT_TRUE(OpenMarkup(R"(<div id="low" data-style-width="(w | min(50)) + 'px'"/>)"
                               R"(<div id="lowest" data-style-width="(w | min(70, 60)) + 'px'"/>)"
                               R"(<div id="high" data-style-width="(w | max(150)) + 'px'"/>)"
                               R"(<div id="clamped" data-style-width="(w | clamp(20, 80)) + 'px'"/>)",
                               "div { height: 10px; }", RenderExtent{ 400, 200 }, desc)
                        .IsValid());
        Refresh();
    }

    void Publish(double width)
    {
        ASSERT_TRUE(Ui().SetValue(Screen(), UiPropertyIdAt(0), UiValue(width)));
        Refresh();
    }

    [[nodiscard]] float Width(std::string_view id) const
    {
        return BoxOf(id, UiBoxArea::Content).value_or(UiElementBox{}).Width;
    }
};
} // namespace

TEST_F(UiDataTransformTest, EachTransformBindsTheNumberItNames)
{
    Open(100.0);
    EXPECT_FLOAT_EQ(Width("low"), 50.0f);
    EXPECT_FLOAT_EQ(Width("lowest"), 60.0f);
    EXPECT_FLOAT_EQ(Width("high"), 150.0f);
    EXPECT_FLOAT_EQ(Width("clamped"), 80.0f);

    Publish(10.0);
    EXPECT_FLOAT_EQ(Width("low"), 10.0f);
    EXPECT_FLOAT_EQ(Width("lowest"), 10.0f);
    EXPECT_FLOAT_EQ(Width("high"), 150.0f);
    EXPECT_FLOAT_EQ(Width("clamped"), 20.0f);

    Publish(55.0);
    EXPECT_FLOAT_EQ(Width("clamped"), 55.0f);
    EXPECT_TRUE(NoErrors());
}

TEST_F(UiDataTransformTest, ATransformGivenTooFewValuesSaysSoAndBindsNothing)
{
    UiScreenDesc desc;
    desc.ModelName = "transforms";
    desc.Properties = { UiModelProperty{ "w", UiValue(100.0) } };
    ASSERT_TRUE(OpenMarkup(R"(<div id="short" data-style-width="(w | clamp(20)) + 'px'"/>)"
                           R"(<div id="alone" data-style-width="(w | min()) + 'px'"/>)",
                           "div { height: 10px; width: 7px; }", RenderExtent{ 400, 200 }, desc)
                    .IsValid());
    Refresh();

    EXPECT_FLOAT_EQ(BoxOf("short", UiBoxArea::Content).value_or(UiElementBox{}).Width, 7.0f);
    EXPECT_FLOAT_EQ(BoxOf("alone", UiBoxArea::Content).value_or(UiElementBox{}).Width, 7.0f);
    std::size_t named = 0;
    for (const UiDiagnostic& diagnostic : Ui().DrainDiagnostics())
        named += diagnostic.Message.find("'clamp'") != std::string::npos
              || diagnostic.Message.find("'min'") != std::string::npos;
    EXPECT_GE(named, 2u) << "a misused transform failed silently";
}
