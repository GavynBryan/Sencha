#include <gtest/gtest.h>

#include <assets/runtime/RuntimeAssets.h>
#include <assets/ui/UiPackage.h>
#include <assets/ui/UiPackageSerializer.h>
#include <core/assets/AssetRegistry.h>
#include <core/logging/LoggingProvider.h>
#include <ui/UiService.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <span>
#include <string>
#include <vector>

// Gradient decorators: what a document asks for reaches the renderer as a
// gradient over the element's own shape, with a colour ramp made once and
// handed over like any generated texture, and released with the decorator.
namespace
{
std::vector<std::byte> BytesOf(std::string_view text)
{
    std::vector<std::byte> out(text.size());
    std::memcpy(out.data(), text.data(), text.size());
    return out;
}

class TempAssetRoot
{
public:
    TempAssetRoot()
    {
        Root = std::filesystem::temp_directory_path()
               / ("sencha_ui_gradient_test_" + std::to_string(std::random_device{}()));
        std::filesystem::create_directories(Root / "ui");
    }
    ~TempAssetRoot()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
    TempAssetRoot(const TempAssetRoot&) = delete;
    TempAssetRoot& operator=(const TempAssetRoot&) = delete;

    void WritePackage(std::string_view markup, std::string_view style) const
    {
        UiPackage package;
        package.RootDocumentName = "screen.rml";
        package.Blobs.push_back({ .VirtualName = "screen.rml", .SourcePath = "ui/screen.rml",
                                  .Kind = UiBlobKind::Document, .Bytes = BytesOf(markup) });
        package.Blobs.push_back({ .VirtualName = "screen.rcss", .SourcePath = "ui/screen.rcss",
                                  .Kind = UiBlobKind::StyleSheet, .Bytes = BytesOf(style) });
        std::vector<std::byte> bytes;
        ASSERT_TRUE(WriteSuiToBytes(package, bytes));
        std::ofstream(Root / "ui" / "screen.sui", std::ios::binary)
            .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    [[nodiscard]] std::string PathString() const { return Root.generic_string(); }

private:
    std::filesystem::path Root;
};

struct Texel
{
    int R, G, B, A;
};

class UiGradientTest : public testing::Test
{
protected:
    // Lays out one 200 x 40 px element, #g, at the surface's corner, styled by
    // `style`, and extracts it.
    void Open(std::string_view style)
    {
        Root.WritePackage(R"(<rml><head><link type="text/rcss" href="screen.rcss"/></head>)"
                          R"(<body><div id="g"/></body></rml>)",
                          std::string("body { display: block; width: 100%; height: 100%; } "
                                      "#g { display: block; position: absolute; left: 0px; top: 0px; "
                                      "width: 200px; height: 40px; ")
                              + std::string(style) + " }");
        ScanAssetsDirectory(Root.PathString(), Assets.Registry, Assets.Assets.Kinds());
        Ui = std::make_unique<UiService>(Logging, Assets.Assets, Assets.UiPackages, Assets.Fonts, nullptr);
        ASSERT_TRUE(Ui->IsReady());
        const UiSurfaceId surface = Ui->CreateSurface("test", RenderExtent{ 400, 200 });
        Screen = Ui->OpenScreen(surface, "asset://ui/screen.sui");
        ASSERT_TRUE(Screen.IsValid());
        Extract();
    }

    void Extract()
    {
        Ui->Update();
        Ui->ExtractRender();
        const UiTextureChanges changes = Ui->TakeTextureChanges();
        for (const UiTextureUpload& upload : changes.Uploads)
            Uploads.push_back(upload);
    }

    // The one gradient command in the frame.
    const UiDrawCommand* Gradient() const
    {
        const UiDrawCommand* found = nullptr;
        for (const UiDrawFrame& frame : Ui->Frames())
            for (const UiDrawCommand& command : frame.Commands)
                if (command.Gradient.has_value())
                {
                    EXPECT_EQ(found, nullptr) << "more than one gradient was drawn";
                    found = &command;
                }
        return found;
    }

    const UiTextureUpload* Upload(UiGeneratedTextureId id) const
    {
        const auto it = std::find_if(Uploads.begin(), Uploads.end(),
                                     [id](const UiTextureUpload& upload) { return upload.Id == id; });
        return it != Uploads.end() ? &*it : nullptr;
    }

    // The texel `index` of the gradient's ramp, as handed over.
    Texel Ramp(std::uint32_t index) const
    {
        const UiDrawCommand* command = Gradient();
        if (command == nullptr)
            return {};
        const UiTextureUpload* upload = Upload(command->Gradient->Ramp);
        if (upload == nullptr)
            return {};
        const std::byte* texel = upload->PremultipliedRgba8.data() + static_cast<std::size_t>(index) * 4;
        return { std::to_integer<int>(texel[0]), std::to_integer<int>(texel[1]),
                 std::to_integer<int>(texel[2]), std::to_integer<int>(texel[3]) };
    }

    void TearDown() override
    {
        if (Ui)
            Ui->Shutdown();
    }

    TempAssetRoot Root;
    LoggingProvider Logging;
    ComponentSerializerRegistry Serializers;
    RuntimeAssets Assets{ Logging, Serializers, RuntimeAssets::ReferenceOnly{} };
    std::unique_ptr<UiService> Ui;
    UiScreenHandle Screen;
    std::vector<UiTextureUpload> Uploads;
};

constexpr std::uint32_t kLast = kUiGradientRampTexels - 1;
} // namespace

TEST_F(UiGradientTest, ALinearGradientIsRecordedOverTheElementsBoxWithItsRamp)
{
    Open("decorator: linear-gradient(90deg, #ff0000, #0000ff);");
    const UiDrawCommand* command = Gradient();
    ASSERT_NE(command, nullptr) << "the gradient drew nothing";
    const UiGradient& gradient = *command->Gradient;
    EXPECT_EQ(gradient.Shape, UiGradientShape::Linear);
    EXPECT_FALSE(gradient.Repeating);
    // To the right, across the box's width.
    EXPECT_NEAR(gradient.Origin.X, 0.0f, 0.01f);
    EXPECT_NEAR(gradient.Axis.X, 200.0f, 0.01f);
    EXPECT_NEAR(gradient.Axis.Y, 0.0f, 0.01f);
    EXPECT_FLOAT_EQ(gradient.FirstStop, 0.0f);
    EXPECT_FLOAT_EQ(gradient.LastStop, 1.0f);

    // The ramp is the texture, so residency and comparison treat it as one.
    EXPECT_EQ(command->Texture.Kind, UiTextureKind::Generated);
    EXPECT_EQ(command->Texture.Generated, gradient.Ramp);
    const UiTextureUpload* ramp = Upload(gradient.Ramp);
    ASSERT_NE(ramp, nullptr) << "the ramp was never handed over";
    EXPECT_EQ(ramp->Size.Width, kUiGradientRampTexels);
    EXPECT_EQ(ramp->Size.Height, 1u);
    EXPECT_EQ(ramp->PremultipliedRgba8.size(), kUiGradientRampTexels * 4u);

    // Its ends are the stops, its middle halfway between them in sRGB.
    const Texel first = Ramp(0), last = Ramp(kLast), middle = Ramp(kUiGradientRampTexels / 2);
    EXPECT_EQ(first.R, 255); EXPECT_EQ(first.B, 0); EXPECT_EQ(first.A, 255);
    EXPECT_EQ(last.R, 0); EXPECT_EQ(last.B, 255); EXPECT_EQ(last.A, 255);
    EXPECT_NEAR(middle.R, 128, 1);
    EXPECT_NEAR(middle.B, 127, 1);

    // The geometry's UV is the position in the box, which the gradient is in.
    ASSERT_TRUE(command->Geometry);
    for (const UiVertex& vertex : command->Geometry->Vertices)
    {
        EXPECT_NEAR(vertex.Uv.X, vertex.Position.X, 0.01f);
        EXPECT_NEAR(vertex.Uv.Y, vertex.Position.Y, 0.01f);
    }
}

TEST_F(UiGradientTest, ColoursBlendWithTheirAlphaAsCssBlendsThem)
{
    // Premultiplied: fading red to clear never passes through dark red.
    Open("decorator: linear-gradient(90deg, #ff0000ff, #ff000000);");
    const Texel middle = Ramp(kUiGradientRampTexels / 2);
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
    // Eight pixels of a 200 px gradient line.
    EXPECT_NEAR(command->Gradient->LastStop, 8.0f / 200.0f, 1e-4f);
    // White for the first half of the period, black for the second.
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
    // From 90 degrees: the turn starts a quarter round.
    EXPECT_NEAR(command->Gradient->Axis.X, 0.0f, 1e-5f);
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

TEST_F(UiGradientTest, AnUnchangedDocumentRecordsTheSameGradientAndUploadsNothingNew)
{
    Open("decorator: linear-gradient(90deg, #ff0000, #0000ff);");
    ASSERT_FALSE(Ui->Frames().empty());
    const UiDrawFrame before = Ui->Frames().front();
    const std::size_t uploaded = Uploads.size();
    Extract();
    ASSERT_FALSE(Ui->Frames().empty());
    EXPECT_TRUE(SameUiRecording(before, Ui->Frames().front()))
        << "a still gradient would redraw a texture surface every frame";
    EXPECT_EQ(Uploads.size(), uploaded) << "the ramp was made again";
}

TEST_F(UiGradientTest, ClosingTheScreenReleasesTheRamp)
{
    Open("decorator: linear-gradient(90deg, #ff0000, #0000ff);");
    const UiDrawCommand* command = Gradient();
    ASSERT_NE(command, nullptr);
    const UiGeneratedTextureId ramp = command->Gradient->Ramp;
    Ui->CloseScreen(Screen);
    Ui->Update();
    Ui->ExtractRender();
    const UiTextureChanges changes = Ui->TakeTextureChanges();
    EXPECT_NE(std::find(changes.Released.begin(), changes.Released.end(), ramp), changes.Released.end())
        << "a closed screen's ramp stays resident";
}
