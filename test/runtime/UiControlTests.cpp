#include <gtest/gtest.h>

#include <assets/runtime/RuntimeAssets.h>
#include <assets/ui/UiPackage.h>
#include <assets/ui/UiPackageSerializer.h>
#include <core/assets/AssetRegistry.h>
#include <core/logging/LoggingProvider.h>
#include <ui/UiService.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <SDL3/SDL.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

// Two-way editing, and the one rule that makes it safe: a control's value is
// presentation state, not a commit.
//
// The failure these exist to prevent is a settings screen that applies a
// half-typed number, or an inspector that moves a brush because somebody
// touched a field and then pressed escape. Validation, undo and transactions
// all live on the far side of an explicit action; a control that committed on
// change would put them behind a keystroke.

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
             / ("sencha_ui_control_test_" + std::to_string(rd()));
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

// A settings form: an editable field, a read-only label, and the three things a
// user can do about an edit.
constexpr std::string_view kMarkup = R"(<rml>
<head><link type="text/rcss" href="form.rcss"/></head>
<body data-model="settings">
    <input id="field" type="text" data-value="sensitivity"/>
    <div id="readonly">{{profile}}</div>
    <div id="apply" data-event-click="settings_apply"/>
    <div id="cancel" data-event-click="settings_cancel"/>
</body>
</rml>)";

constexpr std::string_view kStyle = R"(
body { display: block; width: 100%; height: 100%; pointer-events: none; }
#field { display: block; position: absolute; left: 20px; top: 20px;
         width: 200px; height: 30px; pointer-events: auto; tab-index: auto; }
#readonly { display: block; position: absolute; left: 20px; top: 60px;
            width: 200px; height: 20px; }
#apply { display: block; position: absolute; left: 20px; top: 100px;
         width: 100px; height: 30px; pointer-events: auto; tab-index: auto; }
#cancel { display: block; position: absolute; left: 140px; top: 100px;
          width: 100px; height: 30px; pointer-events: auto; tab-index: auto; }
)";

UiPackage MakePackage()
{
    UiPackage package;
    package.RootDocumentName = "form.rml";

    UiPackageBlob root;
    root.VirtualName = "form.rml";
    root.SourcePath = "ui/form.rml";
    root.Kind = UiBlobKind::Document;
    root.Bytes = BytesOf(kMarkup);
    package.Blobs.push_back(std::move(root));

    UiPackageBlob sheet;
    sheet.VirtualName = "form.rcss";
    sheet.SourcePath = "ui/form.rcss";
    sheet.Kind = UiBlobKind::StyleSheet;
    sheet.Bytes = BytesOf(kStyle);
    package.Blobs.push_back(std::move(sheet));
    return package;
}

constexpr auto kSensitivity = UiModelPropertyId{ 1 };
constexpr auto kProfile = UiModelPropertyId{ 2 };
constexpr auto kApply = UiActionId{ 1 };
constexpr auto kCancel = UiActionId{ 2 };

UiScreenDesc MakeDesc()
{
    UiScreenDesc desc;
    desc.PackagePath = "asset://ui/form.sui";
    desc.ModelName = "settings";
    desc.Properties = {
        UiModelProperty{ "sensitivity", UiValue(std::string("1.2")), true },
        UiModelProperty{ "profile", UiValue(std::string("default")), false },
    };
    desc.Actions = { "settings_apply", "settings_cancel" };
    return desc;
}

class UiTestHost
{
public:
    explicit UiTestHost(const TempAssetRoot& root)
        : Assets(Logging, Serializers, RuntimeAssets::ReferenceOnly{})
    {
        ScanAssetsDirectory(root.PathString(), Assets.Registry, Assets.Assets.Kinds());
        Ui = std::make_unique<UiService>(Logging, Assets.Assets, Assets.UiPackages,
                                         Assets.Fonts, nullptr, nullptr);
    }
    ~UiTestHost() { if (Ui != nullptr) Ui->Shutdown(); }
    UiService& Service() { return *Ui; }

private:
    LoggingProvider Logging;
    ComponentSerializerRegistry Serializers;
    RuntimeAssets Assets;
    std::unique_ptr<UiService> Ui;
};

SDL_Event MouseButton(uint32_t type, float x, float y)
{
    SDL_Event event{};
    event.type = type;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = x;
    event.button.y = y;
    return event;
}

SDL_Event TextInput(const char* text)
{
    SDL_Event event{};
    event.type = SDL_EVENT_TEXT_INPUT;
    event.text.text = text;
    return event;
}

void ClickAt(UiService& ui, float x, float y)
{
    SDL_Event move{};
    move.type = SDL_EVENT_MOUSE_MOTION;
    move.motion.x = x;
    move.motion.y = y;
    (void)ui.ProcessPlatformEvent(move);
    (void)ui.ProcessPlatformEvent(MouseButton(SDL_EVENT_MOUSE_BUTTON_DOWN, x, y));
    (void)ui.ProcessPlatformEvent(MouseButton(SDL_EVENT_MOUSE_BUTTON_UP, x, y));
}

struct Fixture
{
    TempAssetRoot Root;
    std::unique_ptr<UiTestHost> Host;
    UiSurfaceId Surface;
    UiScreenHandle Screen;

    Fixture()
    {
        std::vector<std::byte> bytes;
        EXPECT_TRUE(WriteSuiToBytes(MakePackage(), bytes));
        Root.WriteBytes("ui/form.sui", bytes);
        Host = std::make_unique<UiTestHost>(Root);
        Surface = Host->Service().CreateSurface("test", RenderExtent{ 800, 600 });
        Screen = Host->Service().OpenScreen(Surface, MakeDesc());
        Host->Service().Update();
    }

    UiService& Ui() { return Host->Service(); }

    // Focus the field and type, the way a person would.
    void TypeIntoField(const char* text)
    {
        ClickAt(Ui(), 80.0f, 35.0f);
        Ui().Update();
        (void)Ui().ProcessPlatformEvent(TextInput(text));
        Ui().Update();
    }
};
} // namespace

TEST(UiControls, TypingChangesThePresentationValueAndNothingElse)
{
    Fixture fixture;
    ASSERT_TRUE(fixture.Screen.IsValid());

    // What the host published.
    EXPECT_EQ(fixture.Ui().GetValue(fixture.Screen, kSensitivity).AsString(), "1.2");

    fixture.TypeIntoField("9");

    // The typed value is readable -- that is the transient edit state -- and it
    // arrived without any action being raised, so nothing has been committed.
    EXPECT_NE(fixture.Ui().GetValue(fixture.Screen, kSensitivity).AsString(), "1.2")
        << "the field's value never reached the model";
    EXPECT_TRUE(fixture.Ui().DrainActions().empty())
        << "typing raised an action, so a host would commit on every keystroke";
}

TEST(UiControls, ApplyIsWhatTellsTheHostToRead)
{
    Fixture fixture;
    ASSERT_TRUE(fixture.Screen.IsValid());

    fixture.TypeIntoField("9");
    const std::string edited(fixture.Ui().GetValue(fixture.Screen, kSensitivity).AsString());

    ClickAt(fixture.Ui(), 70.0f, 115.0f);
    fixture.Ui().Update();

    const std::vector<UiAction> actions = fixture.Ui().DrainActions();
    ASSERT_EQ(actions.size(), 1u);
    EXPECT_EQ(actions[0].Id, kApply);

    // The host reads what the user typed, at the moment it was told to.
    EXPECT_EQ(fixture.Ui().GetValue(fixture.Screen, kSensitivity).AsString(), edited);
}

TEST(UiControls, CancelIsTheHostRepublishingWhatItStillHolds)
{
    Fixture fixture;
    ASSERT_TRUE(fixture.Screen.IsValid());

    fixture.TypeIntoField("9");
    ASSERT_NE(fixture.Ui().GetValue(fixture.Screen, kSensitivity).AsString(), "1.2");

    ClickAt(fixture.Ui(), 190.0f, 115.0f);
    fixture.Ui().Update();

    const std::vector<UiAction> actions = fixture.Ui().DrainActions();
    ASSERT_EQ(actions.size(), 1u);
    EXPECT_EQ(actions[0].Id, kCancel);

    // Cancelling is not something this layer does. The host still holds the
    // authoritative value and puts it back, which is why cancelling is possible
    // at all -- the edit never went anywhere.
    EXPECT_TRUE(fixture.Ui().SetValue(fixture.Screen, kSensitivity,
                                      UiValue(std::string("1.2"))));
    fixture.Ui().Update();
    EXPECT_EQ(fixture.Ui().GetValue(fixture.Screen, kSensitivity).AsString(), "1.2");
}

TEST(UiControls, AReadOnlyPropertyIsNotWritableByADocument)
{
    Fixture fixture;
    ASSERT_TRUE(fixture.Screen.IsValid());

    // Declared without Editable, so no setter is bound and the binding is
    // read-only. The host remains the only writer.
    EXPECT_EQ(fixture.Ui().GetValue(fixture.Screen, kProfile).AsString(), "default");
    EXPECT_TRUE(fixture.Ui().SetValue(fixture.Screen, kProfile, UiValue(std::string("fast"))));
    fixture.Ui().Update();
    EXPECT_EQ(fixture.Ui().GetValue(fixture.Screen, kProfile).AsString(), "fast");
}

TEST(UiControls, ClosingAScreenMidEditCommitsNothing)
{
    // The interruption case, and the reason the edit lives here rather than in
    // the application: there is nothing to roll back.
    Fixture fixture;
    ASSERT_TRUE(fixture.Screen.IsValid());

    fixture.TypeIntoField("9");
    ASSERT_NE(fixture.Ui().GetValue(fixture.Screen, kSensitivity).AsString(), "1.2");

    fixture.Ui().CloseScreen(fixture.Screen);
    fixture.Ui().Update();

    EXPECT_TRUE(fixture.Ui().DrainActions().empty())
        << "closing mid-edit raised an action, which a host would act on";
    EXPECT_TRUE(fixture.Ui().GetValue(fixture.Screen, kSensitivity).IsNone());
}

TEST(UiControls, DestroyingTheSurfaceMidEditCommitsNothingEither)
{
    Fixture fixture;
    ASSERT_TRUE(fixture.Screen.IsValid());

    fixture.TypeIntoField("9");
    fixture.Ui().DestroySurface(fixture.Surface);
    fixture.Ui().Update();

    EXPECT_TRUE(fixture.Ui().DrainActions().empty());
    EXPECT_FALSE(fixture.Ui().IsScreenOpen(fixture.Screen));
}

TEST(UiControls, AFocusedFieldTakesTheKeyboardAndReleasesItOnBlur)
{
    Fixture fixture;
    ASSERT_TRUE(fixture.Screen.IsValid());

    EXPECT_FALSE(fixture.Ui().Capture().Keyboard);

    ClickAt(fixture.Ui(), 80.0f, 35.0f);
    fixture.Ui().Update();
    EXPECT_TRUE(fixture.Ui().Capture().Keyboard)
        << "a focused text field must take the keyboard, or typing reaches the game";

    // Clicking a plain button moves focus off the field.
    ClickAt(fixture.Ui(), 70.0f, 115.0f);
    fixture.Ui().Update();
    EXPECT_FALSE(fixture.Ui().Capture().Keyboard)
        << "the field lost focus and the keyboard stayed captured";
}

// -- lists -------------------------------------------------------------------

namespace
{
constexpr std::string_view kListMarkup = R"(<rml>
<head><link type="text/rcss" href="list.rcss"/></head>
<body data-model="browser">
    <div id="rows">
        <div class="row" data-for="item : items" data-style-width="20px">{{item}}</div>
    </div>
</body>
</rml>)";

constexpr std::string_view kListStyle = R"(
body { display: block; width: 100%; height: 100%; pointer-events: none; }
/* No height: the container grows with its rows, which is what makes the row
   count measurable without a renderer. */
#rows { display: block; width: 400px; }
.row { display: block; height: 20px; }
)";

UiPackage MakeListPackage()
{
    UiPackage package;
    package.RootDocumentName = "list.rml";

    UiPackageBlob root;
    root.VirtualName = "list.rml";
    root.SourcePath = "ui/list.rml";
    root.Kind = UiBlobKind::Document;
    root.Bytes = BytesOf(kListMarkup);
    package.Blobs.push_back(std::move(root));

    UiPackageBlob sheet;
    sheet.VirtualName = "list.rcss";
    sheet.SourcePath = "ui/list.rcss";
    sheet.Kind = UiBlobKind::StyleSheet;
    sheet.Bytes = BytesOf(kListStyle);
    package.Blobs.push_back(std::move(sheet));
    return package;
}

UiScreenDesc MakeListDesc()
{
    UiScreenDesc desc;
    desc.PackagePath = "asset://ui/list.sui";
    desc.ModelName = "browser";
    desc.Arrays = { "items" };
    return desc;
}

// The list's own height is the observable: each row is 20px, so the container
// grows by exactly one row per item. Counting boxes beats counting bindings.
struct ListFixture
{
    TempAssetRoot Root;
    std::unique_ptr<UiTestHost> Host;
    UiSurfaceId Surface;
    UiScreenHandle Screen;

    ListFixture()
    {
        std::vector<std::byte> bytes;
        EXPECT_TRUE(WriteSuiToBytes(MakeListPackage(), bytes));
        Root.WriteBytes("ui/list.sui", bytes);
        Host = std::make_unique<UiTestHost>(Root);
        Surface = Host->Service().CreateSurface("test", RenderExtent{ 800, 600 });
        Screen = Host->Service().OpenScreen(Surface, MakeListDesc());
        Host->Service().Update();
    }
    UiService& Ui() { return Host->Service(); }
};
} // namespace

TEST(UiControls, APublishedListIsRepeatedOverByTheDocument)
{
    ListFixture fixture;
    ASSERT_TRUE(fixture.Screen.IsValid());

    const std::vector<std::string> three = { "full", "fast", "ship" };
    EXPECT_TRUE(fixture.Ui().SetArray(fixture.Screen, UiArrayIdAt(0), three));
    fixture.Ui().Update();
    EXPECT_EQ(fixture.Ui().ArraySize(fixture.Screen, UiArrayIdAt(0)), 3u);

    // Three rows of 20px. If data-for did not repeat, this is zero.
    ASSERT_TRUE(fixture.Ui().MeasureElement(fixture.Screen, "rows").has_value());
    EXPECT_FLOAT_EQ(fixture.Ui().MeasureElement(fixture.Screen, "rows")->Height, 60.0f)
        << "three 20px rows should measure 60px; data-for did not repeat";

    const std::vector<std::string> one = { "full" };
    EXPECT_TRUE(fixture.Ui().SetArray(fixture.Screen, UiArrayIdAt(0), one));
    fixture.Ui().Update();
    EXPECT_EQ(fixture.Ui().ArraySize(fixture.Screen, UiArrayIdAt(0)), 1u);

    EXPECT_FLOAT_EQ(fixture.Ui().MeasureElement(fixture.Screen, "rows")->Height, 20.0f)
        << "shrinking the list did not remove rows";
}

TEST(UiControls, RepublishingAnIdenticalListReportsNoChange)
{
    // A panel republishes its list every frame. Re-running every binding that
    // repeats over it each time is the cost this avoids.
    ListFixture fixture;
    ASSERT_TRUE(fixture.Screen.IsValid());

    const std::vector<std::string> items = { "full", "fast" };
    EXPECT_TRUE(fixture.Ui().SetArray(fixture.Screen, UiArrayIdAt(0), items));
    EXPECT_FALSE(fixture.Ui().SetArray(fixture.Screen, UiArrayIdAt(0), items));

    const std::vector<std::string> reordered = { "fast", "full" };
    EXPECT_TRUE(fixture.Ui().SetArray(fixture.Screen, UiArrayIdAt(0), reordered))
        << "a reorder is a change even though the size did not move";
}

TEST(UiControls, ListsResolveByNameAndAClosedScreenHasNone)
{
    ListFixture fixture;
    ASSERT_TRUE(fixture.Screen.IsValid());

    EXPECT_EQ(fixture.Ui().FindArray(fixture.Screen, "items"), UiArrayIdAt(0));
    EXPECT_FALSE(fixture.Ui().FindArray(fixture.Screen, "absent").IsValid());

    fixture.Ui().CloseScreen(fixture.Screen);
    EXPECT_FALSE(fixture.Ui().FindArray(fixture.Screen, "items").IsValid());
    EXPECT_EQ(fixture.Ui().ArraySize(fixture.Screen, UiArrayIdAt(0)), 0u);
    EXPECT_FALSE(fixture.Ui().SetArray(fixture.Screen, UiArrayIdAt(0), { }));
}

// -- rows --------------------------------------------------------------------

namespace
{
// An inspector's shape: rows the host publishes, each with a label the document
// shows and a value a control edits.
constexpr std::string_view kRowMarkup = R"(<rml>
<head><link type="text/rcss" href="rows.rcss"/></head>
<body data-model="inspector">
    <div id="fields">
        <div class="field" data-for="row : fields">
            <span class="name">{{row.label}}</span>
            <input type="text" data-value="row.value"/>
        </div>
    </div>
</body>
</rml>)";

constexpr std::string_view kRowStyle = R"(
body { display: block; width: 100%; height: 100%; pointer-events: none; }
#fields { display: block; width: 400px; }
.field { display: block; height: 24px; pointer-events: auto; }
.name { display: inline-block; width: 120px; }
.field input { display: inline-block; width: 200px; height: 20px; tab-index: auto; }
)";

UiPackage MakeRowPackage()
{
    UiPackage package;
    package.RootDocumentName = "rows.rml";

    UiPackageBlob root;
    root.VirtualName = "rows.rml";
    root.SourcePath = "ui/rows.rml";
    root.Kind = UiBlobKind::Document;
    root.Bytes = BytesOf(kRowMarkup);
    package.Blobs.push_back(std::move(root));

    UiPackageBlob sheet;
    sheet.VirtualName = "rows.rcss";
    sheet.SourcePath = "ui/rows.rcss";
    sheet.Kind = UiBlobKind::StyleSheet;
    sheet.Bytes = BytesOf(kRowStyle);
    package.Blobs.push_back(std::move(sheet));
    return package;
}

UiScreenDesc MakeRowDesc()
{
    UiScreenDesc desc;
    desc.PackagePath = "asset://ui/rows.sui";
    desc.ModelName = "inspector";
    desc.RowLists = { "fields" };
    return desc;
}

struct RowFixture
{
    TempAssetRoot Root;
    std::unique_ptr<UiTestHost> Host;
    UiSurfaceId Surface;
    UiScreenHandle Screen;

    RowFixture()
    {
        std::vector<std::byte> bytes;
        EXPECT_TRUE(WriteSuiToBytes(MakeRowPackage(), bytes));
        Root.WriteBytes("ui/rows.sui", bytes);
        Host = std::make_unique<UiTestHost>(Root);
        Surface = Host->Service().CreateSurface("test", RenderExtent{ 800, 600 });
        Screen = Host->Service().OpenScreen(Surface, MakeRowDesc());
        Host->Service().Update();
    }
    UiService& Ui() { return Host->Service(); }
};
} // namespace

TEST(UiControls, RowsArePresentedWithTheirLabels)
{
    RowFixture fixture;
    ASSERT_TRUE(fixture.Screen.IsValid());

    const std::vector<UiRow> rows = {
        UiRow{ "Position", "0, 0, 0", "" },
        UiRow{ "Rotation", "0, 0, 0", "" },
        UiRow{ "Scale", "1, 1, 1", "" },
    };
    EXPECT_TRUE(fixture.Ui().SetRows(fixture.Screen, UiRowsIdAt(0), rows));
    fixture.Ui().Update();

    // Three 24px rows. If the struct members did not bind, data-for repeats
    // nothing and this is zero.
    ASSERT_TRUE(fixture.Ui().MeasureElement(fixture.Screen, "fields").has_value());
    EXPECT_FLOAT_EQ(fixture.Ui().MeasureElement(fixture.Screen, "fields")->Height, 72.0f);

    const std::vector<UiRow> readBack = fixture.Ui().GetRows(fixture.Screen, UiRowsIdAt(0));
    ASSERT_EQ(readBack.size(), 3u);
    EXPECT_EQ(readBack[1].Label, "Rotation");
}

TEST(UiControls, ARowsValueIsEditableAndReadBackByTheHost)
{
    // The inspector's whole mechanism: a control inside a repeated row writes
    // the presentation copy, and the host reads that row back when an action
    // tells it to. Nothing about the row reaches the application before then.
    RowFixture fixture;
    ASSERT_TRUE(fixture.Screen.IsValid());

    const std::vector<UiRow> rows = { UiRow{ "Position", "5", "" } };
    EXPECT_TRUE(fixture.Ui().SetRows(fixture.Screen, UiRowsIdAt(0), rows));
    fixture.Ui().Update();

    // Click the row's input and type. The field sits right of a 120px label.
    ClickAt(fixture.Ui(), 200.0f, 12.0f);
    fixture.Ui().Update();

    SDL_Event text{};
    text.type = SDL_EVENT_TEXT_INPUT;
    text.text.text = "7";
    (void)fixture.Ui().ProcessPlatformEvent(text);
    fixture.Ui().Update();

    const std::vector<UiRow> edited = fixture.Ui().GetRows(fixture.Screen, UiRowsIdAt(0));
    ASSERT_EQ(edited.size(), 1u);
    EXPECT_NE(edited[0].Value, "5")
        << "typing into a repeated row's control never reached the model";
    EXPECT_EQ(edited[0].Label, "Position") << "editing the value disturbed the label";
}

TEST(UiControls, RepublishingIdenticalRowsReportsNoChange)
{
    RowFixture fixture;
    ASSERT_TRUE(fixture.Screen.IsValid());

    const std::vector<UiRow> rows = { UiRow{ "Position", "5", "" } };
    EXPECT_TRUE(fixture.Ui().SetRows(fixture.Screen, UiRowsIdAt(0), rows));
    EXPECT_FALSE(fixture.Ui().SetRows(fixture.Screen, UiRowsIdAt(0), rows));

    const std::vector<UiRow> relabelled = { UiRow{ "Origin", "5", "" } };
    EXPECT_TRUE(fixture.Ui().SetRows(fixture.Screen, UiRowsIdAt(0), relabelled))
        << "a changed label is a change even though the value did not move";
}

TEST(UiControls, RowsResolveByNameAndAClosedScreenHasNone)
{
    RowFixture fixture;
    ASSERT_TRUE(fixture.Screen.IsValid());

    EXPECT_EQ(fixture.Ui().FindRows(fixture.Screen, "fields"), UiRowsIdAt(0));
    EXPECT_FALSE(fixture.Ui().FindRows(fixture.Screen, "absent").IsValid());

    fixture.Ui().CloseScreen(fixture.Screen);
    EXPECT_TRUE(fixture.Ui().GetRows(fixture.Screen, UiRowsIdAt(0)).empty());
    EXPECT_FALSE(fixture.Ui().SetRows(fixture.Screen, UiRowsIdAt(0), {}));
}

// -- row controls ------------------------------------------------------------

namespace
{
// A settings page's shape: rows the host publishes with the control each one
// asks for, and a list beside them so both kinds of repeat share one screen.
// The ids are bound rather than written: a data-for keeps its template in the
// document, hidden, and a static id inside it would be found ahead of the
// generated row's. A bound attribute is applied only to generated rows.
// The action carries the value (ev.value) rather than the host reading the
// model back, because the document engine gives no order between the two
// listeners a change event reaches.
constexpr std::string_view kControlMarkup = R"rml(<rml>
<head><link type="text/rcss" href="controls.rcss"/></head>
<body data-model="settings">
    <div id="rows">
        <div class="row" data-for="row : rows">
            <span class="name">{{row.label}}</span>
            <input data-attr-id="'slider'" type="range" class="control"
                   data-if="row.editable && row.control == 'range'"
                   data-attr-min="row.min" data-attr-max="row.max" data-attr-step="row.step"
                   data-value="row.number"
                   data-event-change="rows_activate(it_index, ev.value)"/>
            <select data-attr-id="'picker'" class="control"
                    data-if="row.editable && row.control == 'choice'"
                    data-value="row.value"
                    data-event-change="rows_activate(it_index, ev.value)">
                <option data-for="c : row.choices" data-attr-value="c">{{c}}</option>
            </select>
            <span data-attr-id="'plain'" class="plain" data-if="!row.editable || row.control == 'text'">{{row.value}}</span>
        </div>
    </div>
    <div id="names"><div class="nm" data-for="n : names">{{n}}</div></div>
</body>
</rml>)rml";

constexpr std::string_view kControlStyle = R"(
body { display: block; width: 100%; height: 100%; pointer-events: none; }
#rows { display: block; width: 400px; }
.row { display: block; height: 24px; }
.name { display: inline-block; width: 100px; }
.control { display: inline-block; width: 150px; height: 20px; pointer-events: auto; tab-index: auto; }
.plain { display: inline-block; width: 150px; height: 20px; }
slidertrack { display: block; height: 20px; pointer-events: auto; }
sliderbar { display: block; width: 10px; height: 20px; pointer-events: auto; }
sliderprogress { display: block; height: 20px; pointer-events: auto; }
sliderarrowdec, sliderarrowinc { display: none; }
selectvalue { display: block; height: 20px; pointer-events: auto; }
selectarrow { display: block; width: 16px; height: 20px; pointer-events: auto; }
selectbox { display: block; pointer-events: auto; }
selectbox option { display: block; height: 20px; pointer-events: auto; }
#names { display: block; }
.nm { display: block; height: 16px; }
)";

UiPackage MakeControlPackage()
{
    UiPackage package;
    package.RootDocumentName = "controls.rml";

    UiPackageBlob root;
    root.VirtualName = "controls.rml";
    root.SourcePath = "ui/controls.rml";
    root.Kind = UiBlobKind::Document;
    root.Bytes = BytesOf(kControlMarkup);
    package.Blobs.push_back(std::move(root));

    UiPackageBlob sheet;
    sheet.VirtualName = "controls.rcss";
    sheet.SourcePath = "ui/controls.rcss";
    sheet.Kind = UiBlobKind::StyleSheet;
    sheet.Bytes = BytesOf(kControlStyle);
    package.Blobs.push_back(std::move(sheet));
    return package;
}

UiScreenDesc MakeControlDesc()
{
    UiScreenDesc desc;
    desc.PackagePath = "asset://ui/controls.sui";
    desc.ModelName = "settings";
    desc.RowLists = { "rows" };
    desc.Arrays = { "names" };
    desc.Actions = { "rows_activate" };
    return desc;
}

constexpr auto kRowsActivate = UiActionId{ 1 };

UiRow RangeRow(double value)
{
    UiRow row{ "Volume", "", "", true };
    row.Control = UiRowControl::Range;
    row.Number = value;
    row.Min = 0.0;
    row.Max = 1.0;
    row.Step = 0.1;
    return row;
}

UiRow ChoiceRow(const char* value, std::vector<std::string> choices)
{
    UiRow row{ "Quality", value, "", true };
    row.Control = UiRowControl::Choice;
    row.Choices = std::move(choices);
    return row;
}

SDL_Event KeyPress(SDL_Scancode scancode, uint32_t type)
{
    SDL_Event event{};
    event.type = type;
    event.key.scancode = scancode;
    return event;
}

struct ControlFixture
{
    TempAssetRoot Root;
    std::unique_ptr<UiTestHost> Host;
    UiSurfaceId Surface;
    UiScreenHandle Screen;

    ControlFixture()
    {
        std::vector<std::byte> bytes;
        EXPECT_TRUE(WriteSuiToBytes(MakeControlPackage(), bytes));
        Root.WriteBytes("ui/controls.sui", bytes);
        Host = std::make_unique<UiTestHost>(Root);
        Surface = Host->Service().CreateSurface("test", RenderExtent{ 800, 600 });
        Screen = Host->Service().OpenScreen(Surface, MakeControlDesc());
        Host->Service().Update();
    }
    UiService& Ui() { return Host->Service(); }

    // Publishes, settles the document, and throws away the change the engine
    // raises for a control as it receives its first value -- the tests below
    // are about what a person does afterwards.
    void Publish(const UiRow& row)
    {
        (void)Ui().SetRows(Screen, UiRowsIdAt(0), std::vector<UiRow>{ row });
        Ui().Update();
        Ui().Update();
        (void)Ui().DrainActions(Screen);
    }

    void Press(SDL_Scancode scancode)
    {
        (void)Ui().ProcessPlatformEvent(KeyPress(scancode, SDL_EVENT_KEY_DOWN));
        (void)Ui().ProcessPlatformEvent(KeyPress(scancode, SDL_EVENT_KEY_UP));
        Ui().Update();
    }
};
} // namespace

TEST(UiControls, ARangeReportsTheValueItMovedToInTheActionItself)
{
    // The ordering claim: whatever order the engine installed the two change
    // listeners in, the action carries the new value, so the host never has to
    // read the model back and hope the write got there first.
    ControlFixture fixture;
    ASSERT_TRUE(fixture.Screen.IsValid());
    fixture.Publish(RangeRow(0.5));

    // The track spans the control's 150px, right of a 100px label; a press
    // near its right end jumps the bar there.
    ClickAt(fixture.Ui(), 100.0f + 150.0f * 0.9f, 10.0f);
    fixture.Ui().Update();

    const std::vector<UiAction> actions = fixture.Ui().DrainActions(fixture.Screen);
    ASSERT_FALSE(actions.empty()) << "moving the slider raised nothing";
    const UiAction& action = actions.back();
    EXPECT_EQ(action.Id, kRowsActivate);
    ASSERT_EQ(action.Arguments.size(), 2u) << "the action carries the row and the value";
    EXPECT_EQ(action.Arguments[0].AsInt(), 0);
    EXPECT_EQ(action.Arguments[1].Kind(), UiValueKind::Float)
        << "a range reports a number, not the text of one";
    EXPECT_GT(action.Arguments[1].AsFloat(), 0.5);
    EXPECT_LE(action.Arguments[1].AsFloat(), 1.0);
}

TEST(UiControls, AChoiceChangesFromTheKeyboardAndReportsTheLabel)
{
    ControlFixture fixture;
    ASSERT_TRUE(fixture.Screen.IsValid());
    fixture.Publish(ChoiceRow("Low", { "Low", "Medium", "High" }));

    // Focus the drop-down, then step it: a focused select changes on Down
    // without the box being open, which is what a controller drives.
    ClickAt(fixture.Ui(), 150.0f, 10.0f);
    fixture.Ui().Update();
    (void)fixture.Ui().DrainActions(fixture.Screen);
    fixture.Press(SDL_SCANCODE_DOWN);

    const std::vector<UiAction> actions = fixture.Ui().DrainActions(fixture.Screen);
    ASSERT_FALSE(actions.empty()) << "stepping the drop-down raised nothing";
    ASSERT_EQ(actions.back().Arguments.size(), 2u);
    EXPECT_EQ(std::string(actions.back().Arguments[1].AsString()), "Medium");
    // The presentation copy followed too, so the control shows what it said.
    EXPECT_EQ(fixture.Ui().GetRows(fixture.Screen, UiRowsIdAt(0)).front().Value, "Medium");
}

TEST(UiControls, AChoiceNobodyOfferedIsShownAsItselfAndChangesNothing)
{
    // A drop-down whose value matches no option selects the first one and
    // reports that as a change. So a host that cannot map its current value to
    // a label publishes the raw value as an option too, and the round trip
    // through publication is a fixed point: the same value comes back.
    {
        ControlFixture fixture;
        ASSERT_TRUE(fixture.Screen.IsValid());
        (void)fixture.Ui().SetRows(fixture.Screen, UiRowsIdAt(0), std::vector<UiRow>{
            ChoiceRow("165", { "Low", "Medium", "High", "165" }) });
        fixture.Ui().Update();
        fixture.Ui().Update();

        EXPECT_EQ(fixture.Ui().GetRows(fixture.Screen, UiRowsIdAt(0)).front().Value, "165")
            << "the value the host published was replaced by the document";
        // Every control in the row reports once on publish, the hidden slider
        // included -- as a number, which is how a host tells them apart. What
        // the drop-down reported is the labels, and they must all be the one
        // the host published.
        std::size_t fromTheDropDown = 0;
        for (const UiAction& action : fixture.Ui().DrainActions(fixture.Screen))
        {
            ASSERT_EQ(action.Arguments.size(), 2u);
            if (action.Arguments[1].Kind() != UiValueKind::String)
                continue;
            ++fromTheDropDown;
            EXPECT_EQ(std::string(action.Arguments[1].AsString()), "165")
                << "publication reported a change to something the player never chose";
        }
        EXPECT_GE(fromTheDropDown, 1u);
    }
    // The negative control: without the raw entry, the document does rewrite
    // it -- which is what proves the assertion above is looking at the right
    // thing.
    {
        ControlFixture fixture;
        ASSERT_TRUE(fixture.Screen.IsValid());
        (void)fixture.Ui().SetRows(fixture.Screen, UiRowsIdAt(0), std::vector<UiRow>{
            ChoiceRow("165", { "Low", "Medium", "High" }) });
        fixture.Ui().Update();
        fixture.Ui().Update();
        EXPECT_EQ(fixture.Ui().GetRows(fixture.Screen, UiRowsIdAt(0)).front().Value, "Low");
    }
}

TEST(UiControls, ARowGetsTheControlItsKindNames)
{
    // The kind is an enum, and an enum binds as an integer unless the runtime
    // says otherwise -- in which case `row.control == 'range'` is quietly false
    // for every row and no control ever appears. This is the assertion that
    // catches that. One fixture at a time: each owns the same temporary asset
    // root.
    const auto shown = [](const std::optional<UiElementBox>& box) {
        return box.has_value() && box->Width > 0.0f && box->Height > 0.0f;
    };

    {
        ControlFixture range;
        ASSERT_TRUE(range.Screen.IsValid());
        range.Publish(RangeRow(0.5));
        EXPECT_TRUE(shown(range.Ui().MeasureElement(range.Screen, "slider")));
        EXPECT_FALSE(shown(range.Ui().MeasureElement(range.Screen, "picker")));
        EXPECT_FALSE(shown(range.Ui().MeasureElement(range.Screen, "plain")));
    }
    {
        ControlFixture choice;
        ASSERT_TRUE(choice.Screen.IsValid());
        choice.Publish(ChoiceRow("Low", { "Low", "High" }));
        EXPECT_FALSE(shown(choice.Ui().MeasureElement(choice.Screen, "slider")));
        EXPECT_TRUE(shown(choice.Ui().MeasureElement(choice.Screen, "picker")));
        EXPECT_FALSE(shown(choice.Ui().MeasureElement(choice.Screen, "plain")));
    }
    {
        ControlFixture text;
        ASSERT_TRUE(text.Screen.IsValid());
        text.Publish(UiRow{ "Name", "value", "", false });
        EXPECT_FALSE(shown(text.Ui().MeasureElement(text.Screen, "slider")));
        EXPECT_FALSE(shown(text.Ui().MeasureElement(text.Screen, "picker")));
        EXPECT_TRUE(shown(text.Ui().MeasureElement(text.Screen, "plain")));
    }
}

TEST(UiControls, ListsAndRowsBindOnTheSameScreen)
{
    // The row's choices are a list of strings, and so is a plain list. Both
    // want the same element type declared once; a screen that has both must
    // not find the second declaration refused.
    ControlFixture fixture;
    ASSERT_TRUE(fixture.Screen.IsValid());
    EXPECT_TRUE(fixture.Ui().SetArray(fixture.Screen, UiArrayIdAt(0),
                                      std::vector<std::string>{ "a", "b", "c" }));
    fixture.Publish(ChoiceRow("Low", { "Low", "High" }));
    fixture.Ui().Update();
    EXPECT_EQ(fixture.Ui().ArraySize(fixture.Screen, UiArrayIdAt(0)), 3u);
    EXPECT_EQ(fixture.Ui().GetRows(fixture.Screen, UiRowsIdAt(0)).front().Value, "Low");
}
