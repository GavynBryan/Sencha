#include <gtest/gtest.h>

#include <app/OptionsPage.h>
#include <app/PauseMenuModel.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <core/logging/LoggingProvider.h>
#include <ui/UiService.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <cctype>
#include <fstream>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <vector>

// The document the engine actually ships, opened from the artifacts it actually
// installs. A shipping build has no cooker in it, so engine/assets/.cooked is
// committed -- and a committed artifact that has drifted from its source is the
// kind of failure that looks like nothing is wrong until a player sees it.

namespace
{
class ShellContentHost
{
public:
    ShellContentHost()
        : Assets(Logging, Serializers, RuntimeAssets::ReferenceOnly{})
    {
        const std::string root = std::string(SENCHA_REPO_ROOT) + "/engine/assets";
        ScanAssetsDirectory(root, Assets.Registry, Assets.Assets.Kinds());
        ScanAssetsDirectory(root + "/.cooked", Assets.Registry, Assets.Assets.Kinds());
        RegisterCookedAssets(root, Assets.Registry);
        Ui = std::make_unique<UiService>(Logging, Assets.Assets, Assets.UiPackages,
                                         Assets.Fonts, nullptr, nullptr);
    }
    ~ShellContentHost() { if (Ui != nullptr) Ui->Shutdown(); }

    UiService& Service() { return *Ui; }

private:
    LoggingProvider Logging;
    ComponentSerializerRegistry Serializers;
    RuntimeAssets Assets;
    std::unique_ptr<UiService> Ui;
};

// Nothing refuses a data expression naming something the model does not
// publish: it resolves to nothing and that part of the document renders empty,
// which reads as a row with no label rather than as a fault. The layer reports
// the miss as a diagnostic naming the variable, so every shipped document gets
// checked for one.
void ExpectEveryExpressionResolved(ShellContentHost& host)
{
    for (const UiDiagnostic& entry : host.Service().DrainDiagnostics())
    {
        EXPECT_NE(entry.Kind, UiDiagnosticKind::BindingMissing)
            << "the document reads '" << entry.Variable.value_or("?")
            << "', which its model does not publish, so that part of it renders blank: "
            << entry.Message;
    }
}

// The description the shell opens its root page with, kept in step with
// PauseMenu::DescribeRoot by being asserted against the same document.
UiScreenDesc RootDesc(const PauseMenuModel& model)
{
    UiScreenDesc desc;
    desc.PackagePath = model.RootPage();
    desc.ModelName = "pause";
    desc.Modal = true;
    desc.Properties = { UiModelProperty{ "title", UiValue(model.Title()) } };
    desc.Arrays = { "entries" };
    desc.Actions = { "pause_activate" };
    return desc;
}
}

TEST(ShellDocument, TheShippedPauseDocumentOpensAndBindsWhatTheShellDeclares)
{
    ShellContentHost host;
    UiService& ui = host.Service();
    ASSERT_TRUE(ui.IsReady());

    PauseMenuModel model;
    model.InstallDefaults("Exit to Desktop");

    const UiSurfaceId surface = ui.CreateSurface("shell", RenderExtent{ 1280, 720 });
    const UiScreenHandle screen = ui.OpenScreen(surface, RootDesc(model));
    ASSERT_TRUE(screen.IsValid())
        << "the engine's own pause document did not open: either the committed "
           "artifact is stale (re-run ctest -R CookEngineUiContent) or the "
           "document declares something the shell does not";

    EXPECT_TRUE(ui.SetArray(screen, UiArrayIdAt(0), model.Labels()));
    // Publishing the value it already holds marks nothing -- the layer compares
    // before it dirties -- so a changed one is what proves the binding is live.
    EXPECT_FALSE(ui.SetValue(screen, UiPropertyIdAt(0), UiValue(model.Title())));
    EXPECT_TRUE(ui.SetValue(screen, UiPropertyIdAt(0), UiValue(std::string("Halted"))));
    ui.Update();
    EXPECT_EQ(ui.GetValue(screen, UiPropertyIdAt(0)).AsString(), "Halted");

    // The panel is laid out, which is what says the stylesheet came through the
    // package rather than being looked for on disk.
    const std::optional<UiElementBox> panel = ui.MeasureElement(screen, "panel");
    ASSERT_TRUE(panel.has_value());
    EXPECT_GT(panel->Width, 0.0f);
    EXPECT_GT(panel->Height, 0.0f);

    ExpectEveryExpressionResolved(host);
}

TEST(ShellDocument, TheDocumentRepeatsOverWhateverTheModelPublishes)
{
    // The entries are data. A game adding, renaming or reordering one changes
    // the model and never the markup, so the document has to present a list it
    // was told about rather than rows it was written with.
    ShellContentHost host;
    UiService& ui = host.Service();
    ASSERT_TRUE(ui.IsReady());

    PauseMenuModel model;
    model.InstallDefaults("Exit to Desktop");
    model.SetOptionsPage("asset://ui/options.rml");
    const PauseCommandId save = model.Add("Save Game", [](PauseMenuContext&) {});
    ASSERT_TRUE(save.IsValid());

    const UiSurfaceId surface = ui.CreateSurface("shell", RenderExtent{ 1280, 720 });
    const UiScreenHandle screen = ui.OpenScreen(surface, RootDesc(model));
    ASSERT_TRUE(screen.IsValid());

    ASSERT_TRUE(ui.SetArray(screen, UiArrayIdAt(0), model.Labels()));
    ui.Update();
    EXPECT_EQ(ui.ArraySize(screen, UiArrayIdAt(0)), 4u);

    // Four entries occupy more of the panel than two do, which is the only
    // device-free way to see that the document repeated rather than ignored it.
    const std::optional<UiElementBox> four = ui.MeasureElement(screen, "entries");
    ASSERT_TRUE(four.has_value());

    ASSERT_TRUE(ui.SetArray(screen, UiArrayIdAt(0),
                            std::vector<std::string>{ "Resume", "Exit to Desktop" }));
    ui.Update();
    const std::optional<UiElementBox> two = ui.MeasureElement(screen, "entries");
    ASSERT_TRUE(two.has_value());

    EXPECT_GT(four->Height, two->Height) << "the entry list did not follow the model";
}

TEST(ShellDocument, TheRootPackagePathTheModelDefaultsToIsTheOneThatShips)
{
    // The default has to name the asset the engine installs, or a game that
    // changes nothing gets no menu -- which is the whole claim.
    PauseMenuModel model;
    EXPECT_EQ(model.RootPage(), "asset://ui/pause.rml");
}

TEST(ShellDocument, ThePanelStaysCentredAtAnyViewportAndAnyEntryCount)
{
    // The bug this pins: centring by pulling the panel back half its size needs
    // that size written into the stylesheet, and the height is not something
    // the stylesheet knows -- it grows with however many entries the model
    // published. A hardcoded half is wrong at every viewport, and obviously
    // wrong at one it was not measured at, which is what going fullscreen is.
    ShellContentHost host;
    UiService& ui = host.Service();
    ASSERT_TRUE(ui.IsReady());

    PauseMenuModel model;
    model.InstallDefaults("Exit to Desktop");

    const UiSurfaceId surface = ui.CreateSurface("shell", RenderExtent{ 1280, 720 });
    const UiScreenHandle screen = ui.OpenScreen(surface, RootDesc(model));
    ASSERT_TRUE(screen.IsValid());

    const auto centredIn = [&](std::uint32_t width, std::uint32_t height,
                               std::span<const std::string> entries) {
        ui.SetSurfaceSize(surface, RenderExtent{ width, height });
        // Not asserted: republishing the same list marks nothing, which is the
        // layer comparing before it dirties rather than a failure.
        (void)ui.SetArray(screen, UiArrayIdAt(0), entries);
        ui.Update();

        const std::optional<UiElementBox> panel = ui.MeasureElement(screen, "panel");
        ASSERT_TRUE(panel.has_value());
        ASSERT_GT(panel->Width, 0.0f);
        ASSERT_GT(panel->Height, 0.0f);

        // Measured boxes exclude padding and border, so the panel's own centre
        // is compared rather than its bounds: the slack absorbs that, and a
        // panel pinned to a stale height or a stale viewport misses by far
        // more than it.
        const float slack = 12.0f;
        EXPECT_NEAR(panel->X + panel->Width * 0.5f, static_cast<float>(width) * 0.5f, slack)
            << width << "x" << height << ": not centred horizontally";
        EXPECT_NEAR(panel->Y + panel->Height * 0.5f, static_cast<float>(height) * 0.5f, slack)
            << width << "x" << height << ": not centred vertically";
        EXPECT_LE(panel->Width, static_cast<float>(width))
            << width << "x" << height << ": wider than the viewport";
    };

    const std::vector<std::string> two{ "Resume", "Exit to Desktop" };
    const std::vector<std::string> five{ "Resume", "Options", "Save Game",
                                         "Load Game", "Exit to Desktop" };

    // The size it was authored at, then a fullscreen one, then a small one --
    // each with a short menu and a long one, because the height is what the
    // arithmetic used to get wrong.
    centredIn(1280, 720, two);
    centredIn(1280, 720, five);
    centredIn(3840, 2160, two);
    centredIn(3840, 2160, five);
    centredIn(800, 600, five);
}

TEST(ShellDocument, ADisplayScaleChangeReflowsRatherThanRescales)
{
    // dp is applied by the document engine, so a HiDPI display makes the panel
    // physically larger while the viewport it sits in stays the same size in
    // pixels. It still has to be inside that viewport.
    ShellContentHost host;
    UiService& ui = host.Service();
    ASSERT_TRUE(ui.IsReady());

    PauseMenuModel model;
    model.InstallDefaults("Exit to Desktop");

    const UiSurfaceId surface = ui.CreateSurface("shell", RenderExtent{ 1920, 1080 });
    const UiScreenHandle screen = ui.OpenScreen(surface, RootDesc(model));
    ASSERT_TRUE(screen.IsValid());
    ASSERT_TRUE(ui.SetArray(screen, UiArrayIdAt(0), model.Labels()));
    ui.Update();

    const std::optional<UiElementBox> atOne = ui.MeasureElement(screen, "panel");
    ASSERT_TRUE(atOne.has_value());

    ui.SetSurfaceScale(surface, 2.0f);
    ui.Update();
    const std::optional<UiElementBox> atTwo = ui.MeasureElement(screen, "panel");
    ASSERT_TRUE(atTwo.has_value());

    EXPECT_GT(atTwo->Width, atOne->Width)
        << "authored lengths ignored the display scale: they are px, not dp";
    EXPECT_LE(atTwo->Width, 1920.0f) << "the scaled panel left the viewport";
    EXPECT_NEAR(atTwo->X + atTwo->Width * 0.5f, 960.0f, 40.0f)
        << "scaling moved the panel off centre";
}

TEST(ShellDocument, TheOptionsDocumentReadsTheRowMembersTheRuntimeActuallyBinds)
{
    // The bug this pins: a row's parts are bound under the names the runtime
    // registers them with -- label, value, detail -- and a document asking for
    // `row.Label` is asking for a member that is not there. Nothing refuses it.
    // The expression resolves to nothing, the span renders empty, and every
    // setting on the page appears to have no name, which is exactly what a
    // page with no settings looks like.
    //
    // There is no device-free way to read back what a span rendered, so the
    // warning the document engine emits is the evidence. That makes this a
    // check on the whole document rather than on two member names: any
    // expression the shipped page reads that its model does not publish is
    // caught the same way.
    ShellContentHost host;
    UiService& ui = host.Service();
    ASSERT_TRUE(ui.IsReady());

    OptionsPage page;
    const UiSurfaceId surface = ui.CreateSurface("shell", RenderExtent{ 1280, 720 });
    const UiScreenHandle screen =
        ui.OpenScreen(surface, page.Describe("asset://ui/options.rml"));
    ASSERT_TRUE(screen.IsValid());

    const UiModelRowsId rows = ui.FindRows(screen, "rows");
    ASSERT_TRUE(rows.IsValid());
    // One of each control the page offers, so every member the document reads
    // for a slider and for a drop-down is exercised.
    UiRow volume{ "Master Volume", "0.8", "", true };
    volume.Control = UiRowControl::Range;
    volume.Number = 0.8;
    volume.Min = 0.0;
    volume.Max = 1.0;
    volume.Step = 0.05;
    UiRow mode{ "Display Mode", "Windowed", "", true };
    mode.Control = UiRowControl::Choice;
    mode.Choices = { "Windowed", "Borderless", "Fullscreen" };
    ASSERT_TRUE(ui.SetRows(screen, rows, std::vector<UiRow>{ volume, mode }));
    ui.Update();

    ExpectEveryExpressionResolved(host);
}

namespace
{
std::string ReadRepoFile(const char* relative)
{
    std::ifstream in(std::string(SENCHA_REPO_ROOT) + "/" + relative, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}
}

TEST(ShellDocument, TheStylesheetNamesOnlyPartsTheDocumentEngineActuallyCreates)
{
    // A slider and a drop-down are built out of elements the document engine
    // names for what they are -- slidertrack, selectbox -- and it synthesises
    // no classes: <input type="range"> gets no "range" class, so a rule for
    // input.range styles nothing and fails silently. So every tag selector
    // in the shipped sheet that looks like a widget part must be one the
    // engine creates, and every class the sheet styles must be one the shipped
    // markup sets.
    const std::string sheet = ReadRepoFile("engine/assets/ui/pause.rcss");
    ASSERT_FALSE(sheet.empty());
    const std::string markup = ReadRepoFile("engine/assets/ui/pause.rml")
                             + ReadRepoFile("engine/assets/ui/options.rml");
    ASSERT_FALSE(markup.empty());

    static const std::set<std::string> kEngineParts{
        "slidertrack", "sliderbar", "sliderprogress", "sliderarrowdec", "sliderarrowinc",
        "selectarrow", "selectvalue", "selectbox", "option",
    };

    // Selectors are whatever precedes a '{', split on commas and whitespace.
    std::size_t pos = 0;
    while ((pos = sheet.find('{', pos)) != std::string::npos)
    {
        const std::size_t start = sheet.rfind('}', pos);
        std::string selectors = sheet.substr(start == std::string::npos ? 0 : start + 1,
                                             pos - (start == std::string::npos ? 0 : start + 1));
        ++pos;
        // Strip comments.
        for (std::size_t c; (c = selectors.find("/*")) != std::string::npos;)
        {
            const std::size_t e = selectors.find("*/", c);
            selectors.erase(c, e == std::string::npos ? std::string::npos : e + 2 - c);
        }
        std::string token;
        for (char ch : selectors + " ")
        {
            if (std::isspace(static_cast<unsigned char>(ch)) || ch == ',' || ch == '>')
            {
                if (!token.empty())
                {
                    // Drop pseudo-classes and pseudo-elements.
                    const std::string bare = token.substr(0, token.find(':'));
                    if (!bare.empty() && bare[0] == '.')
                    {
                        EXPECT_NE(markup.find(bare.substr(1)), std::string::npos)
                            << "the sheet styles class '" << bare
                            << "', which no shipped document sets";
                    }
                    else if (bare.rfind("slider", 0) == 0 || bare.rfind("select", 0) == 0)
                    {
                        EXPECT_TRUE(kEngineParts.count(bare) == 1)
                            << "the sheet styles '" << bare
                            << "', which the document engine never creates";
                    }
                    token.clear();
                }
            }
            else
                token.push_back(ch);
        }
    }
}
