#include <sencha/testing/UiLayoutTest.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <string>
#include <vector>

namespace
{
struct Texel
{
    int R, G, B, A;
};

struct Box
{
    std::string_view Style;
    int Top = 0;
    std::string_view Width = "200px";
};

constexpr int kBoxHeight = 40;

class UiGradientTest : public UiLayoutTest
{
protected:
    void Open(std::initializer_list<Box> boxes)
    {
        std::string body;
        std::string style;
        for (std::size_t index = 0; index < boxes.size(); ++index)
        {
            const Box& box = *(boxes.begin() + index);
            const std::string id = "box" + std::to_string(index);
            body += "<div id=\"" + id + "\"/>";
            style += "#" + id + " { display: block; position: absolute; left: 0px; top: " + std::to_string(box.Top)
                + "px; width: " + std::string(box.Width) + "; height: " + std::to_string(kBoxHeight) + "px; "
                + std::string(box.Style) + " } ";
        }
        ASSERT_TRUE(OpenMarkup(body, style, RenderExtent{ 400, 200 }).IsValid());
        Extract();
    }

    void Open(std::string_view style) { Open({ Box{ style } }); }

    UiTextureChanges Extract()
    {
        Ui().Update();
        Ui().ExtractRender();
        UiTextureChanges changes = Ui().TakeTextureChanges();
        Uploads.insert(Uploads.end(), changes.Uploads.begin(), changes.Uploads.end());
        return changes;
    }

    std::vector<const UiDrawCommand*> Commands() const
    {
        std::vector<const UiDrawCommand*> found;
        for (const UiDrawFrame& frame : Ui().Frames())
            for (const UiDrawCommand& command : frame.Commands)
                found.push_back(&command);
        return found;
    }

    std::vector<const UiDrawCommand*> Gradients() const
    {
        std::vector<const UiDrawCommand*> found = Commands();
        std::erase_if(found, [](const UiDrawCommand* command) { return !command->Gradient.has_value(); });
        return found;
    }

    const UiDrawCommand* Gradient() const
    {
        const std::vector<const UiDrawCommand*> found = Gradients();
        EXPECT_LE(found.size(), 1u) << "more than one gradient was drawn";
        return found.empty() ? nullptr : found.front();
    }

    const UiTextureUpload* Upload(UiGeneratedTextureId id) const
    {
        const auto it = std::find_if(Uploads.begin(), Uploads.end(),
                                     [id](const UiTextureUpload& upload) { return upload.Id == id; });
        return it != Uploads.end() ? &*it : nullptr;
    }

    Texel Ramp(std::uint32_t index) const
    {
        const UiDrawCommand* command = Gradient();
        const UiTextureUpload* upload = command != nullptr ? Upload(command->Texture.Generated) : nullptr;
        if (upload == nullptr)
            return {};
        const std::byte* texel = upload->PremultipliedRgba8.data() + static_cast<std::size_t>(index) * 4;
        return { std::to_integer<int>(texel[0]), std::to_integer<int>(texel[1]),
                 std::to_integer<int>(texel[2]), std::to_integer<int>(texel[3]) };
    }

    std::vector<UiTextureUpload> Uploads;
};

constexpr std::uint32_t kMiddle = kUiGradientRampTexels / 2;
constexpr std::uint32_t kLast = kUiGradientRampTexels - 1;

bool Released(const UiTextureChanges& changes, UiGeneratedTextureId id)
{
    return std::find(changes.Released.begin(), changes.Released.end(), id) != changes.Released.end();
}
} // namespace

TEST_F(UiGradientTest, ALinearGradientIsRecordedOverTheElementsBoxWithItsRampAsTheTexture)
{
    Open("decorator: linear-gradient(90deg, #ff0000, #0000ff);");
    const UiDrawCommand* command = Gradient();
    ASSERT_NE(command, nullptr) << "the gradient drew nothing";
    const UiGradient& gradient = *command->Gradient;
    EXPECT_EQ(gradient.Shape, UiGradientShape::Linear);
    EXPECT_FALSE(gradient.Repeating);
    EXPECT_NEAR(gradient.Origin.X, 0.0f, 0.01f);
    EXPECT_NEAR(gradient.Axis.X, 200.0f, 0.01f);
    EXPECT_NEAR(gradient.Axis.Y, 0.0f, 0.01f);
    EXPECT_FLOAT_EQ(gradient.FirstStop, 0.0f);
    EXPECT_FLOAT_EQ(gradient.LastStop, 1.0f);

    ASSERT_EQ(command->Texture.Kind, UiTextureKind::Generated);
    const UiTextureUpload* ramp = Upload(command->Texture.Generated);
    ASSERT_NE(ramp, nullptr) << "the ramp was never handed over";
    EXPECT_EQ(ramp->Size.Width, kUiGradientRampTexels);
    EXPECT_EQ(ramp->Size.Height, 1u);
    EXPECT_EQ(ramp->PremultipliedRgba8.size(), kUiGradientRampTexels * 4u);

    const Texel first = Ramp(0), last = Ramp(kLast), middle = Ramp(kMiddle);
    EXPECT_EQ(first.R, 255); EXPECT_EQ(first.B, 0); EXPECT_EQ(first.A, 255);
    EXPECT_EQ(last.R, 0); EXPECT_EQ(last.B, 255); EXPECT_EQ(last.A, 255);
    EXPECT_NEAR(middle.R, 128, 1) << "interpolated in sRGB, as CSS does";
    EXPECT_NEAR(middle.B, 127, 1);

    ASSERT_TRUE(command->Geometry);
    for (const UiVertex& vertex : command->Geometry->Vertices)
    {
        EXPECT_NEAR(vertex.Uv.X, vertex.Position.X, 0.01f) << "the UV is the position in the box";
        EXPECT_NEAR(vertex.Uv.Y, vertex.Position.Y, 0.01f);
    }
}

TEST_F(UiGradientTest, ColoursBlendWithTheirAlphaAsCssBlendsThem)
{
    Open("decorator: linear-gradient(90deg, #ff0000ff, #ff000000);");
    const Texel middle = Ramp(kMiddle);
    EXPECT_NEAR(middle.A, 128, 1);
    EXPECT_NEAR(middle.R, middle.A, 1) << "a premultiplied red is as red as it is opaque";
    EXPECT_EQ(middle.G, 0);
}

TEST_F(UiGradientTest, ARepeatingGradientSpansOnePeriodOfStops)
{
    Open("decorator: repeating-linear-gradient(90deg, #ffffff 0px, #ffffff 4px, #000000 4px, #000000 8px);");
    const UiDrawCommand* command = Gradient();
    ASSERT_NE(command, nullptr);
    EXPECT_TRUE(command->Gradient->Repeating);
    EXPECT_FLOAT_EQ(command->Gradient->FirstStop, 0.0f);
    EXPECT_NEAR(command->Gradient->LastStop, 8.0f / 200.0f, 1e-4f) << "eight pixels of a 200 px line";
    EXPECT_EQ(Ramp(kUiGradientRampTexels / 8).R, 255);
    EXPECT_EQ(Ramp(kUiGradientRampTexels * 7 / 8).R, 0);
}

TEST_F(UiGradientTest, ARadialGradientCarriesItsCentreAndRadius)
{
    Open("decorator: radial-gradient(circle 20px at 50px 20px, #ffffff, #000000);");
    const UiDrawCommand* command = Gradient();
    ASSERT_NE(command, nullptr);
    EXPECT_EQ(command->Gradient->Shape, UiGradientShape::Radial);
    EXPECT_NEAR(command->Gradient->Origin.X, 50.0f, 0.01f);
    EXPECT_NEAR(command->Gradient->Origin.Y, 20.0f, 0.01f);
    EXPECT_NEAR(command->Gradient->Axis.X, 1.0f / 20.0f, 1e-5f);
    EXPECT_NEAR(command->Gradient->Axis.Y, 1.0f / 20.0f, 1e-5f);
}

TEST_F(UiGradientTest, AConicGradientCarriesItsCentreAndStartingDirection)
{
    Open("decorator: conic-gradient(from 90deg at 50% 50%, #ffffff, #000000);");
    const UiDrawCommand* command = Gradient();
    ASSERT_NE(command, nullptr);
    EXPECT_EQ(command->Gradient->Shape, UiGradientShape::Conic);
    EXPECT_NEAR(command->Gradient->Origin.X, 100.0f, 0.01f);
    EXPECT_NEAR(command->Gradient->Origin.Y, 20.0f, 0.01f);
    EXPECT_NEAR(std::hypot(command->Gradient->Axis.X, command->Gradient->Axis.Y), 1.0f, 1e-5f);
    EXPECT_NEAR(command->Gradient->Axis.X, 0.0f, 1e-5f) << "from 90 degrees starts a quarter turn round";
}

TEST_F(UiGradientTest, AnElementsOpacityReachesTheGradientThroughItsVertexColour)
{
    Open("opacity: 0.5; decorator: linear-gradient(90deg, #ffffff, #ffffff);");
    const UiDrawCommand* command = Gradient();
    ASSERT_NE(command, nullptr);
    ASSERT_TRUE(command->Geometry);
    for (const UiVertex& vertex : command->Geometry->Vertices)
        EXPECT_NEAR(vertex.ColorSrgbPremultiplied[3], 127, 1);
}

TEST_F(UiGradientTest, StraightGradientsDrawAsVertexColoursWithNoShader)
{
    constexpr int kVerticalTop = 60;
    Open({ Box{ "decorator: horizontal-gradient(#ff0000 #0000ff);" },
           Box{ "decorator: vertical-gradient(#ff0000 #0000ff);", kVerticalTop } });
    EXPECT_TRUE(Gradients().empty());
    EXPECT_TRUE(Uploads.empty()) << "a straight gradient needs no ramp";

    int horizontal = 0;
    int vertical = 0;
    for (const UiDrawCommand* command : Commands())
    {
        ASSERT_TRUE(command->Geometry);
        ASSERT_FALSE(command->Geometry->Vertices.empty());
        const auto top = [&](const UiVertex& vertex) { return command->Translation.Y + vertex.Position.Y; };
        const bool isVertical = top(command->Geometry->Vertices.front()) >= kVerticalTop;
        ++(isVertical ? vertical : horizontal);
        for (const UiVertex& vertex : command->Geometry->Vertices)
        {
            const float along = isVertical ? (top(vertex) - kVerticalTop) / static_cast<float>(kBoxHeight)
                                           : (command->Translation.X + vertex.Position.X) / 200.0f;
            EXPECT_NEAR(vertex.ColorSrgbPremultiplied[0], 255.0f * (1.0f - along), 1.0f);
            EXPECT_NEAR(vertex.ColorSrgbPremultiplied[2], 255.0f * along, 1.0f);
        }
    }
    EXPECT_EQ(horizontal, 1);
    EXPECT_EQ(vertical, 1);
}

TEST_F(UiGradientTest, AnUnchangedDocumentRecordsTheSameGradientAndUploadsNothingNew)
{
    Open("decorator: linear-gradient(90deg, #ff0000, #0000ff);");
    ASSERT_FALSE(Ui().Frames().empty());
    const UiDrawFrame before = Ui().Frames().front();
    EXPECT_TRUE(Extract().Uploads.empty()) << "the ramp was made again";
    ASSERT_FALSE(Ui().Frames().empty());
    EXPECT_TRUE(SameUiRecording(before, Ui().Frames().front()))
        << "a still gradient would redraw a texture surface every frame";
}

TEST_F(UiGradientTest, ClosingTheScreenReleasesTheRamp)
{
    Open("decorator: linear-gradient(90deg, #ff0000, #0000ff);");
    ASSERT_NE(Gradient(), nullptr);
    const UiGeneratedTextureId ramp = Gradient()->Texture.Generated;
    Ui().CloseScreen(Screen());
    EXPECT_TRUE(Released(Extract(), ramp)) << "a closed screen's ramp stays resident";
}

TEST_F(UiGradientTest, GradientsWithTheSameColoursShareOneRamp)
{
    constexpr std::string_view kBevel = "decorator: linear-gradient(180deg, #fff1cf 0%, #e6b86c 50%, #7c5a28 100%);";
    Open({ Box{ kBevel }, Box{ kBevel, 60, "30px" } });
    const std::vector<const UiDrawCommand*> drawn = Gradients();
    ASSERT_EQ(drawn.size(), 2u);
    EXPECT_EQ(drawn[0]->Texture.Generated, drawn[1]->Texture.Generated)
        << "two elements with one bevel uploaded it twice";
    EXPECT_EQ(Uploads.size(), 1u);
    EXPECT_NE(drawn[0]->Geometry, drawn[1]->Geometry) << "each is shaped by its own box";
}

TEST_F(UiGradientTest, AResizedElementKeepsItsRamp)
{
    Open({ Box{ "decorator: linear-gradient(90deg, #ff0000 0%, #0000ff 100%);", 0, "50%" } });
    ASSERT_NE(Gradient(), nullptr);
    const UiGeneratedTextureId ramp = Gradient()->Texture.Generated;

    Ui().SetSurfaceSize(Surface(), RenderExtent{ 240, 200 });
    const UiTextureChanges changes = Extract();
    ASSERT_NE(Gradient(), nullptr);
    EXPECT_EQ(Gradient()->Texture.Generated, ramp);
    EXPECT_NEAR(Gradient()->Gradient->Axis.X, 120.0f, 0.01f) << "the gradient did not follow the new size";
    EXPECT_TRUE(changes.Uploads.empty()) << "a resize minted the same ramp again";
    EXPECT_FALSE(Released(changes, ramp));
}
