#include <gtest/gtest.h>

#include <assets/runtime/RuntimeAssets.h>
#include <assets/ui/UiPackage.h>
#include <assets/ui/UiPackageSerializer.h>
#include <core/assets/AssetRegistry.h>
#include <core/logging/LoggingProvider.h>
#include <ui/UiService.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

// The boundary this layer exists to draw: state goes down as copies, intent
// comes back as semantic actions, and neither direction carries a pointer.
//
// Device-free, like everything else about the model. A presentation value is
// not a rendering concern.

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
        std::random_device rd;
        Root = std::filesystem::temp_directory_path()
             / ("sencha_ui_model_test_" + std::to_string(rd()));
        std::filesystem::create_directories(Root);
    }
    ~TempAssetRoot()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
    TempAssetRoot(const TempAssetRoot&) = delete;
    TempAssetRoot& operator=(const TempAssetRoot&) = delete;

    void WriteBytes(std::string_view relPath, std::span<const std::byte> bytes) const
    {
        const std::filesystem::path full = Root / relPath;
        std::filesystem::create_directories(full.parent_path());
        std::ofstream file(full, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    [[nodiscard]] std::string PathString() const { return Root.generic_string(); }

private:
    std::filesystem::path Root;
};

// A HUD shape: a bound value the document presents, and a button that asks for
// something.
//
// The width is bound through data-style-width rather than an interpolated
// style attribute, because the document engine substitutes data expressions in
// text and in data-* attributes, not inside a plain style="". Binding geometry
// is what makes a published number observable without a renderer: a measured
// box is the assertion.
constexpr std::string_view kMarkup = R"(<rml>
<head><link type="text/rcss" href="hud.rcss"/></head>
<body data-model="hud">
    <div id="bar" data-style-width="health + 'px'"/>
    <div id="name">{{weapon}}</div>
    <div id="quit" data-event-click="pause_quit"/>
</body>
</rml>)";

constexpr std::string_view kStyle = R"(
body { display: block; width: 100%; height: 100%; }
#bar { display: block; height: 10px; }
#name { display: block; width: 50px; height: 10px; }
#quit { display: block; width: 20px; height: 20px; }
)";

UiPackage MakePackage()
{
    UiPackage package;
    package.RootDocumentName = "hud.rml";

    UiPackageBlob root;
    root.VirtualName = "hud.rml";
    root.SourcePath = "ui/hud.rml";
    root.Kind = UiBlobKind::Document;
    root.Bytes = BytesOf(kMarkup);
    package.Blobs.push_back(std::move(root));

    UiPackageBlob sheet;
    sheet.VirtualName = "hud.rcss";
    sheet.SourcePath = "ui/hud.rcss";
    sheet.Kind = UiBlobKind::StyleSheet;
    sheet.Bytes = BytesOf(kStyle);
    package.Blobs.push_back(std::move(sheet));
    return package;
}

UiScreenDesc MakeDesc()
{
    UiScreenDesc desc;
    desc.PackagePath = "asset://ui/hud.sui";
    desc.ModelName = "hud";
    desc.Properties = {
        UiModelProperty{ "health", UiValue(100.0) },
        UiModelProperty{ "weapon", UiValue(std::string("none")) },
    };
    desc.Actions = { "pause_quit", "pause_resume" };
    return desc;
}

constexpr auto kHealth = UiModelPropertyId{ 1 };
constexpr auto kWeapon = UiModelPropertyId{ 2 };

class UiTestHost
{
public:
    explicit UiTestHost(const TempAssetRoot& root)
        : Assets(Logging, Serializers, RuntimeAssets::ReferenceOnly{})
    {
        ScanAssetsDirectory(root.PathString(), Assets.Registry, Assets.Assets.Kinds());
        Ui = std::make_unique<UiService>(Logging, Assets.Assets, Assets.UiPackages,
                                         Assets.Fonts, nullptr);
    }
    ~UiTestHost() { if (Ui != nullptr) Ui->Shutdown(); }
    UiService& Service() { return *Ui; }

private:
    LoggingProvider Logging;
    ComponentSerializerRegistry Serializers;
    RuntimeAssets Assets;
    std::unique_ptr<UiService> Ui;
};

void WritePackage(const TempAssetRoot& root)
{
    std::vector<std::byte> bytes;
    ASSERT_TRUE(WriteSuiToBytes(MakePackage(), bytes));
    root.WriteBytes("ui/hud.sui", bytes);
}
} // namespace

TEST(UiModel, APublishedValueReachesTheDocument)
{
    TempAssetRoot root;
    WritePackage(root);
    UiTestHost host(root);
    UiService& ui = host.Service();
    ASSERT_TRUE(ui.IsReady());

    const UiSurfaceId surface = ui.CreateSurface("test", RenderExtent{ 800, 600 });
    const UiScreenHandle screen = ui.OpenScreen(surface, MakeDesc());
    ASSERT_TRUE(screen.IsValid());

    // The initial value, before the host publishes anything.
    ui.Update();
    ASSERT_TRUE(ui.MeasureElement(screen, "bar").has_value());
    EXPECT_FLOAT_EQ(ui.MeasureElement(screen, "bar")->Width, 100.0f);

    EXPECT_TRUE(ui.SetValue(screen, kHealth, UiValue(42.0)));
    ui.Update();
    EXPECT_FLOAT_EQ(ui.MeasureElement(screen, "bar")->Width, 42.0f)
        << "the published value did not reach the binding that reads it";
}

TEST(UiModel, SettingAnUnchangedValueReportsNoChange)
{
    // What keeps a HUD publishing the same number every frame from
    // re-evaluating every binding that reads it.
    TempAssetRoot root;
    WritePackage(root);
    UiTestHost host(root);
    UiService& ui = host.Service();

    const UiSurfaceId surface = ui.CreateSurface("test", RenderExtent{ 800, 600 });
    const UiScreenHandle screen = ui.OpenScreen(surface, MakeDesc());
    ASSERT_TRUE(screen.IsValid());

    EXPECT_TRUE(ui.SetValue(screen, kHealth, UiValue(42.0)));
    EXPECT_FALSE(ui.SetValue(screen, kHealth, UiValue(42.0)));
    EXPECT_TRUE(ui.SetValue(screen, kHealth, UiValue(43.0)));

    // And a value of a different kind is a change even when it reads the same.
    EXPECT_TRUE(ui.SetValue(screen, kWeapon, UiValue(std::string("rifle"))));
    EXPECT_FALSE(ui.SetValue(screen, kWeapon, UiValue(std::string("rifle"))));
}

TEST(UiModel, ValuesRoundTripThroughTheModelWithoutChangingKind)
{
    TempAssetRoot root;
    WritePackage(root);
    UiTestHost host(root);
    UiService& ui = host.Service();

    const UiSurfaceId surface = ui.CreateSurface("test", RenderExtent{ 800, 600 });
    const UiScreenHandle screen = ui.OpenScreen(surface, MakeDesc());
    ASSERT_TRUE(screen.IsValid());

    EXPECT_TRUE(ui.SetValue(screen, kWeapon, UiValue::MakeId(0xDEADBEEFCAFEull)));
    const UiValue read = ui.GetValue(screen, kWeapon);
    EXPECT_EQ(read.Kind(), UiValueKind::Id);
    EXPECT_EQ(read.AsId(), 0xDEADBEEFCAFEull)
        << "an identity must survive the round trip exactly; a float would not";
    EXPECT_EQ(read.AsInt(-1), -1) << "an identity is not an integer to do arithmetic on";
}

TEST(UiModel, PropertiesAndActionsResolveByTheNameTheyWereDeclaredUnder)
{
    TempAssetRoot root;
    WritePackage(root);
    UiTestHost host(root);
    UiService& ui = host.Service();

    const UiSurfaceId surface = ui.CreateSurface("test", RenderExtent{ 800, 600 });
    const UiScreenHandle screen = ui.OpenScreen(surface, MakeDesc());
    ASSERT_TRUE(screen.IsValid());

    EXPECT_EQ(ui.FindProperty(screen, "health"), kHealth);
    EXPECT_EQ(ui.FindProperty(screen, "weapon"), kWeapon);
    EXPECT_FALSE(ui.FindProperty(screen, "not-declared").IsValid());

    EXPECT_EQ(ui.FindAction(screen, "pause_quit"), UiActionIdAt(0));
    EXPECT_EQ(ui.FindAction(screen, "pause_resume"), UiActionIdAt(1));
    EXPECT_FALSE(ui.FindAction(screen, "pause_explode").IsValid());
}

TEST(UiModel, AnUndeclaredPropertyIsRefusedRatherThanCreated)
{
    TempAssetRoot root;
    WritePackage(root);
    UiTestHost host(root);
    UiService& ui = host.Service();

    const UiSurfaceId surface = ui.CreateSurface("test", RenderExtent{ 800, 600 });
    const UiScreenHandle screen = ui.OpenScreen(surface, MakeDesc());
    ASSERT_TRUE(screen.IsValid());

    EXPECT_FALSE(ui.SetValue(screen, UiModelPropertyId{ 99 }, UiValue(1.0)));
    EXPECT_TRUE(ui.GetValue(screen, UiModelPropertyId{ 99 }).IsNone());
}

TEST(UiModel, AClosedScreenAnswersNothingAndAcceptsNothing)
{
    TempAssetRoot root;
    WritePackage(root);
    UiTestHost host(root);
    UiService& ui = host.Service();

    const UiSurfaceId surface = ui.CreateSurface("test", RenderExtent{ 800, 600 });
    const UiScreenHandle screen = ui.OpenScreen(surface, MakeDesc());
    ASSERT_TRUE(screen.IsValid());
    ui.CloseScreen(screen);

    EXPECT_FALSE(ui.SetValue(screen, kHealth, UiValue(1.0)));
    EXPECT_TRUE(ui.GetValue(screen, kHealth).IsNone());
    EXPECT_FALSE(ui.FindProperty(screen, "health").IsValid());
}

TEST(UiModel, DrainingTakesEachActionExactlyOnce)
{
    TempAssetRoot root;
    WritePackage(root);
    UiTestHost host(root);
    UiService& ui = host.Service();

    const UiSurfaceId surface = ui.CreateSurface("test", RenderExtent{ 800, 600 });
    ASSERT_TRUE(ui.OpenScreen(surface, MakeDesc()).IsValid());

    // Nothing has been asked for, so nothing comes back -- and draining an
    // empty queue is not an error a host has to guard against.
    EXPECT_TRUE(ui.DrainActions().empty());
    EXPECT_TRUE(ui.DrainActions().empty());
}

TEST(UiModel, ADocumentDeclaringAnUnknownModelDoesNotOpen)
{
    // The document's data-model attribute names "hud". A screen that declared
    // something else leaves those bindings unresolvable, and a screen that
    // presents nothing it was asked to present is worse than one that refused.
    TempAssetRoot root;
    WritePackage(root);
    UiTestHost host(root);
    UiService& ui = host.Service();

    UiScreenDesc desc = MakeDesc();
    desc.ModelName = "something-else";

    const UiSurfaceId surface = ui.CreateSurface("test", RenderExtent{ 800, 600 });
    const UiScreenHandle screen = ui.OpenScreen(surface, desc);
    ASSERT_TRUE(screen.IsValid());

    // The document still opens -- the engine reports the unresolved bindings --
    // but the property it should have driven is not driven by it.
    ui.Update();
    EXPECT_TRUE(ui.SetValue(screen, kHealth, UiValue(42.0)));
    ui.Update();
    ASSERT_TRUE(ui.MeasureElement(screen, "bar").has_value());
    EXPECT_NE(ui.MeasureElement(screen, "bar")->Width, 42.0f)
        << "a mis-declared model drove the document anyway";
}

TEST(UiModel, ANameADataExpressionCannotBindIsRefusedWithTheReason)
{
    // A dot reads as member access, so "pause.quit" binds nothing and the first
    // sign is a button that silently does not work. Refusing at open is the
    // only point where the author is still looking at the declaration.
    TempAssetRoot root;
    WritePackage(root);
    UiTestHost host(root);
    UiService& ui = host.Service();
    const UiSurfaceId surface = ui.CreateSurface("test", RenderExtent{ 800, 600 });

    UiScreenDesc dottedAction = MakeDesc();
    dottedAction.Actions = { "pause.quit" };
    EXPECT_FALSE(ui.OpenScreen(surface, dottedAction).IsValid());

    UiScreenDesc dottedProperty = MakeDesc();
    dottedProperty.Properties = { UiModelProperty{ "hud.health", UiValue(1.0) } };
    EXPECT_FALSE(ui.OpenScreen(surface, dottedProperty).IsValid());

    UiScreenDesc leadingDigit = MakeDesc();
    leadingDigit.Properties = { UiModelProperty{ "2health", UiValue(1.0) } };
    EXPECT_FALSE(ui.OpenScreen(surface, leadingDigit).IsValid());

    // And the legal one still opens, so the check is not just refusing things.
    EXPECT_TRUE(ui.OpenScreen(surface, MakeDesc()).IsValid());
}
