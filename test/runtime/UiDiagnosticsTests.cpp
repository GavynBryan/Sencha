#include <gtest/gtest.h>

#include <assets/runtime/RuntimeAssets.h>
#include <assets/ui/UiPackage.h>
#include <assets/ui/UiPackageSerializer.h>
#include <core/assets/AssetRegistry.h>
#include <core/logging/LoggingProvider.h>
#include <ui/UiService.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

//=============================================================================
// What the UI layer reports, as data.
//
// The rule under test: everything the layer or the document engine notices
// arrives once through DrainDiagnostics, saying where it came from and what
// happened, carrying exactly the attribution the layer genuinely had -- and no
// more. A tool reads these; it never scrapes the log.
//=============================================================================

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
             / ("sencha_ui_diag_test_" + std::to_string(rd()));
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

UiPackage MakePackage(std::string_view name, std::string_view markup, std::string_view style)
{
    UiPackage package;
    package.RootDocumentName = std::string(name) + ".rml";

    UiPackageBlob root;
    root.VirtualName = package.RootDocumentName;
    root.SourcePath = "ui/" + package.RootDocumentName;
    root.Kind = UiBlobKind::Document;
    root.Bytes = BytesOf(markup);
    package.Blobs.push_back(std::move(root));

    UiPackageBlob sheet;
    sheet.VirtualName = std::string(name) + ".rcss";
    sheet.SourcePath = "ui/" + sheet.VirtualName;
    sheet.Kind = UiBlobKind::StyleSheet;
    sheet.Bytes = BytesOf(style);
    package.Blobs.push_back(std::move(sheet));
    return package;
}

constexpr std::string_view kStyle = R"(
body { display: block; width: 100%; height: 100%; pointer-events: none; }
#button { display: block; position: absolute; left: 20px; top: 20px;
          width: 100px; height: 30px; pointer-events: auto; }
.row { display: block; height: 20px; }
)";

class Host
{
public:
    explicit Host(const TempAssetRoot& root)
        : Assets(Logging, Serializers, RuntimeAssets::ReferenceOnly{})
    {
        ScanAssetsDirectory(root.PathString(), Assets.Registry, Assets.Assets.Kinds());
        Ui = std::make_unique<UiService>(Logging, Assets.Assets, Assets.UiPackages,
                                         Assets.Fonts, nullptr, nullptr);
    }
    ~Host() { if (Ui != nullptr) Ui->Shutdown(); }
    UiService& Service() { return *Ui; }
    RuntimeAssets& Runtime() { return Assets; }

private:
    LoggingProvider Logging;
    ComponentSerializerRegistry Serializers;
    RuntimeAssets Assets;
    std::unique_ptr<UiService> Ui;
};

// A root with one cooked package under ui/<name>.sui, opened as asset://ui/<name>.sui.
struct Fixture
{
    TempAssetRoot Root;
    std::unique_ptr<Host> Owner;
    UiSurfaceId Surface;

    explicit Fixture(std::initializer_list<UiPackage> packages)
    {
        for (const UiPackage& package : packages)
        {
            std::vector<std::byte> bytes;
            EXPECT_TRUE(WriteSuiToBytes(package, bytes));
            const std::string name = package.RootDocumentName.substr(
                0, package.RootDocumentName.size() - 4);
            Root.WriteBytes("ui/" + name + ".sui", bytes);
        }
        Owner = std::make_unique<Host>(Root);
        Surface = Ui().CreateSurface("test", RenderExtent{ 800, 600 });
    }
    UiService& Ui() { return Owner->Service(); }

    [[nodiscard]] static std::string PathOf(std::string_view name)
    {
        return "asset://ui/" + std::string(name) + ".sui";
    }
};

std::vector<UiDiagnostic> OfKind(const std::vector<UiDiagnostic>& all, UiDiagnosticKind kind)
{
    std::vector<UiDiagnostic> out;
    for (const UiDiagnostic& d : all)
        if (d.Kind == kind)
            out.push_back(d);
    return out;
}

void ClickAt(UiService& ui, float x, float y)
{
    SDL_Event move{};
    move.type = SDL_EVENT_MOUSE_MOTION;
    move.motion.x = x;
    move.motion.y = y;
    (void)ui.ProcessPlatformEvent(move);
    SDL_Event down{};
    down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    down.button.button = SDL_BUTTON_LEFT;
    down.button.x = x;
    down.button.y = y;
    (void)ui.ProcessPlatformEvent(down);
    SDL_Event up = down;
    up.type = SDL_EVENT_MOUSE_BUTTON_UP;
    (void)ui.ProcessPlatformEvent(up);
}
} // namespace

TEST(UiDiagnostics, AMissingBindingNamesTheVariableTheSurfaceAndTheOnlyScreen)
{
    Fixture f({ MakePackage("doc", R"(<rml><head><link type="text/rcss" href="doc.rcss"/></head>
<body data-model="m"><div>{{title}}</div></body></rml>)", kStyle) });
    UiScreenDesc desc;
    desc.PackagePath = Fixture::PathOf("doc");
    desc.ModelName = "m";   // declares nothing: the document asks for a variable nobody published
    const UiScreenHandle screen = f.Ui().OpenScreen(f.Surface, desc);
    ASSERT_TRUE(screen.IsValid());
    f.Ui().Update();

    const auto misses = OfKind(f.Ui().DrainDiagnostics(), UiDiagnosticKind::BindingMissing);
    ASSERT_FALSE(misses.empty()) << "a binding the model lacks went unreported";
    const UiDiagnostic& miss = misses.front();
    EXPECT_EQ(miss.Source, UiDiagnosticSource::DocumentEngine);
    EXPECT_EQ(miss.Severity, UiDiagnosticSeverity::Warning);
    ASSERT_TRUE(miss.Variable.has_value());
    EXPECT_EQ(*miss.Variable, "title");
    EXPECT_EQ(miss.Surface, f.Surface);
    // A variable's address is resolved while the document loads, inside the
    // scope that names the screen being loaded -- so this is attributed to the
    // screen outright, however many the surface carries.
    EXPECT_EQ(miss.Screen, screen);
}

TEST(UiDiagnostics, WithTwoScreensOnASurfaceAnUpdateTimeMissNamesOnlyTheSurface)
{
    // Two moments, two levels of knowledge. A variable's address is resolved
    // while its document loads, so that miss names the screen whatever else is
    // on the surface. A value or struct member is fetched while the surface
    // updates every model at once, and the engine does not say whose -- so
    // with two screens the layer names the surface and leaves the screen
    // invalid rather than guessing.
    Fixture f({
        MakePackage("a", R"(<rml><head><link type="text/rcss" href="a.rcss"/></head>
<body data-model="a"><div>{{missing_in_a}}</div><div class="row" data-for="row : rows">{{row.nope}}</div></body></rml>)", kStyle),
        MakePackage("b", R"(<rml><head><link type="text/rcss" href="b.rcss"/></head>
<body data-model="b"><div>{{present}}</div></body></rml>)", kStyle),
    });
    UiScreenDesc a;
    a.PackagePath = Fixture::PathOf("a");
    a.ModelName = "a";
    a.RowLists = { "rows" };
    UiScreenDesc b;
    b.PackagePath = Fixture::PathOf("b");
    b.ModelName = "b";
    b.Properties = { UiModelProperty{ "present", UiValue(std::string("yes")), false } };
    const UiScreenHandle screenA = f.Ui().OpenScreen(f.Surface, a);
    ASSERT_TRUE(screenA.IsValid());
    ASSERT_TRUE(f.Ui().OpenScreen(f.Surface, b).IsValid());
    ASSERT_TRUE(f.Ui().SetRows(screenA, UiRowsIdAt(0), std::vector<UiRow>{ UiRow{ "x", "1", "" } }));
    f.Ui().Update();
    f.Ui().Update();

    const auto misses = OfKind(f.Ui().DrainDiagnostics(), UiDiagnosticKind::BindingMissing);
    bool sawLoadTime = false;
    bool sawUpdateTime = false;
    for (const UiDiagnostic& miss : misses)
    {
        EXPECT_EQ(miss.Surface, f.Surface);
        ASSERT_TRUE(miss.Variable.has_value());
        if (*miss.Variable == "missing_in_a")
        {
            sawLoadTime = true;
            EXPECT_EQ(miss.Screen, screenA) << "resolved during A's load: A's";
        }
        else if (miss.Variable->find("nope") != std::string::npos)
        {
            sawUpdateTime = true;
            EXPECT_FALSE(miss.Screen.IsValid())
                << "fetched during the surface's update with two screens on it: nobody's to claim";
        }
    }
    EXPECT_TRUE(sawLoadTime);
    EXPECT_TRUE(sawUpdateTime);
}

TEST(UiDiagnostics, AMissingRowMemberIsAMissingBinding)
{
    // The options-label bug: a document reading row.Label against a member
    // registered as label. Two engine messages describe one mistake; both
    // arrive as the same kind, naming the member.
    Fixture f({ MakePackage("rows", R"(<rml><head><link type="text/rcss" href="rows.rcss"/></head>
<body data-model="m"><div class="row" data-for="row : rows">{{row.Label}}</div></body></rml>)", kStyle) });
    UiScreenDesc desc;
    desc.PackagePath = Fixture::PathOf("rows");
    desc.ModelName = "m";
    desc.RowLists = { "rows" };
    const UiScreenHandle screen = f.Ui().OpenScreen(f.Surface, desc);
    ASSERT_TRUE(screen.IsValid());
    ASSERT_TRUE(f.Ui().SetRows(screen, UiRowsIdAt(0), std::vector<UiRow>{ UiRow{ "x", "1", "" } }));
    f.Ui().Update();
    f.Ui().Update();

    const std::vector<UiDiagnostic> all = f.Ui().DrainDiagnostics();
    const auto members = OfKind(all, UiDiagnosticKind::MemberMissing);
    const auto paths = OfKind(all, UiDiagnosticKind::BindingMissing);
    // The terse report names the member; the full one names the path through
    // the list. Both describe one mistake, and a consumer can join them.
    ASSERT_FALSE(members.empty()) << "the member itself went unreported";
    EXPECT_EQ(members.front().Variable.value_or(""), "Label");
    ASSERT_FALSE(paths.empty());
    EXPECT_NE(paths.front().Variable.value_or("").find("Label"), std::string::npos);
}

TEST(UiDiagnostics, AnUndeclaredActionIsReportedWhenItFiresAndNotBefore)
{
    Fixture f({ MakePackage("act", R"(<rml><head><link type="text/rcss" href="act.rcss"/></head>
<body data-model="m"><div id="button" data-event-click="nope_action"/></body></rml>)", kStyle) });
    UiScreenDesc desc;
    desc.PackagePath = Fixture::PathOf("act");
    desc.ModelName = "m";
    ASSERT_TRUE(f.Ui().OpenScreen(f.Surface, desc).IsValid());
    f.Ui().Update();
    EXPECT_TRUE(OfKind(f.Ui().DrainDiagnostics(), UiDiagnosticKind::EventCallbackMissing).empty())
        << "an action is observable only when its event fires; nothing has fired";

    ClickAt(f.Ui(), 60.0f, 35.0f);
    f.Ui().Update();
    const auto misses = OfKind(f.Ui().DrainDiagnostics(), UiDiagnosticKind::EventCallbackMissing);
    ASSERT_FALSE(misses.empty());
    EXPECT_EQ(misses.front().Variable.value_or(""), "nope_action");
    EXPECT_EQ(misses.front().Surface, f.Surface);
}

TEST(UiDiagnostics, ACookNoteArrivesWhenTheScreenOpensWithItsFileAndLine)
{
    UiPackage package = MakePackage("styled", R"(<rml><head><link type="text/rcss" href="styled.rcss"/></head>
<body><div/></body></rml>)", kStyle);
    package.Unsupported.push_back(UiUnsupportedFeature{ "box-shadow", "ui/styled.rcss", 12 });
    Fixture f({ package });
    const UiScreenHandle screen = f.Ui().OpenScreen(f.Surface, Fixture::PathOf("styled"));
    ASSERT_TRUE(screen.IsValid());

    const auto notes = OfKind(f.Ui().DrainDiagnostics(), UiDiagnosticKind::UnsupportedStyle);
    ASSERT_EQ(notes.size(), 1u);
    EXPECT_EQ(notes.front().Source, UiDiagnosticSource::Cook);
    EXPECT_EQ(notes.front().Severity, UiDiagnosticSeverity::Warning);
    EXPECT_EQ(notes.front().Path.value_or(""), "ui/styled.rcss");
    EXPECT_EQ(notes.front().Line.value_or(0u), 12u);
    EXPECT_EQ(notes.front().Screen, screen);
    EXPECT_EQ(notes.front().Surface, f.Surface);
    EXPECT_NE(notes.front().Message.find("box-shadow"), std::string::npos);
}

TEST(UiDiagnostics, AnUnresolvableResourceRefusesTheScreenAndSaysWhich)
{
    UiPackage package = MakePackage("img", R"(<rml><head><link type="text/rcss" href="img.rcss"/></head>
<body><img src="missing.png"/></body></rml>)", kStyle);
    package.Resources.push_back(AssetRef{ AssetType::Texture, "asset://ui/missing.png" });
    Fixture f({ package });
    EXPECT_FALSE(f.Ui().OpenScreen(f.Surface, Fixture::PathOf("img")).IsValid());

    const auto refusals = OfKind(f.Ui().DrainDiagnostics(), UiDiagnosticKind::ResourceUnresolved);
    ASSERT_EQ(refusals.size(), 1u);
    EXPECT_EQ(refusals.front().Source, UiDiagnosticSource::Runtime);
    EXPECT_EQ(refusals.front().Severity, UiDiagnosticSeverity::Error);
    EXPECT_EQ(refusals.front().Path.value_or(""), "asset://ui/missing.png");
    EXPECT_EQ(refusals.front().Surface, f.Surface);
    EXPECT_FALSE(refusals.front().Screen.IsValid()) << "no screen existed to attribute to";
}

TEST(UiDiagnostics, AMissingPackageAndARefusedModelAreReported)
{
    Fixture f({ MakePackage("ok", R"(<rml><head><link type="text/rcss" href="ok.rcss"/></head>
<body data-model="m"><div/></body></rml>)", kStyle) });

    EXPECT_FALSE(f.Ui().OpenScreen(f.Surface, "asset://ui/nowhere.sui").IsValid());
    auto drained = f.Ui().DrainDiagnostics();
    const auto missing = OfKind(drained, UiDiagnosticKind::PackageUnavailable);
    ASSERT_EQ(missing.size(), 1u);
    EXPECT_EQ(missing.front().Path.value_or(""), "asset://ui/nowhere.sui");

    UiScreenDesc desc;
    desc.PackagePath = Fixture::PathOf("ok");
    desc.ModelName = "m";
    desc.Properties = { UiModelProperty{ "not.bindable", UiValue(1), false } };
    EXPECT_FALSE(f.Ui().OpenScreen(f.Surface, desc).IsValid());
    drained = f.Ui().DrainDiagnostics();
    const auto refused = OfKind(drained, UiDiagnosticKind::ModelRefused);
    ASSERT_EQ(refused.size(), 1u);
    EXPECT_EQ(refused.front().Surface, f.Surface);
    EXPECT_NE(refused.front().Message.find("not.bindable"), std::string::npos);
}

TEST(UiDiagnostics, ARebuildThatFailsKeepsTheOldDocumentAndSaysSo)
{
    Fixture f({ MakePackage("live", R"(<rml><head><link type="text/rcss" href="live.rcss"/></head>
<body><div id="button"/></body></rml>)", kStyle) });
    const UiScreenHandle screen = f.Ui().OpenScreen(f.Surface, Fixture::PathOf("live"));
    ASSERT_TRUE(screen.IsValid());
    f.Ui().Update();
    (void)f.Ui().DrainDiagnostics();

    // An edit whose root document is gone lands in the cache as the reloader
    // would land it: the package is valid, the document it names is not there.
    UiPackage broken = MakePackage("live", "<rml><body/></rml>", kStyle);
    broken.RootDocumentName = "gone.rml";
    ASSERT_TRUE(f.Owner->Runtime().UiPackages.ReloadInPlace(Fixture::PathOf("live"), std::move(broken)));
    f.Ui().Update();

    const auto failed = OfKind(f.Ui().DrainDiagnostics(), UiDiagnosticKind::RebuildFailed);
    ASSERT_EQ(failed.size(), 1u);
    EXPECT_EQ(failed.front().Screen, screen);
    EXPECT_EQ(failed.front().Surface, f.Surface);
    EXPECT_EQ(failed.front().Path.value_or(""), Fixture::PathOf("live"));
    EXPECT_TRUE(f.Ui().IsScreenOpen(screen)) << "the previous document is left alone";
}

TEST(UiDiagnostics, TheFeedIsDrainedOnceAndBoundedWithASequenceGap)
{
    // One hundred and fifty rows each reading a member that is not there: two
    // engine messages per row, well past the ring.
    Fixture f({ MakePackage("many", R"(<rml><head><link type="text/rcss" href="many.rcss"/></head>
<body data-model="m"><div class="row" data-for="row : rows">{{row.nope}}</div></body></rml>)", kStyle) });
    UiScreenDesc desc;
    desc.PackagePath = Fixture::PathOf("many");
    desc.ModelName = "m";
    desc.RowLists = { "rows" };
    const UiScreenHandle screen = f.Ui().OpenScreen(f.Surface, desc);
    ASSERT_TRUE(screen.IsValid());
    std::vector<UiRow> rows(150, UiRow{ "x", "1", "" });
    ASSERT_TRUE(f.Ui().SetRows(screen, UiRowsIdAt(0), rows));
    f.Ui().Update();
    f.Ui().Update();

    const std::vector<UiDiagnostic> drained = f.Ui().DrainDiagnostics();
    ASSERT_EQ(drained.size(), 256u) << "the ring is bounded";
    EXPECT_GT(drained.front().Sequence, 1u) << "the gap says the oldest were dropped";
    for (std::size_t i = 1; i < drained.size(); ++i)
        EXPECT_EQ(drained[i].Sequence, drained[i - 1].Sequence + 1) << "the kept run is contiguous";
    EXPECT_TRUE(f.Ui().DrainDiagnostics().empty()) << "a second drain sees nothing";
}
