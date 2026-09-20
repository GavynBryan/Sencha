#include <gtest/gtest.h>

#include <assets/runtime/RuntimeAssets.h>
#include <assets/ui/UiPackage.h>
#include <assets/ui/UiPackageSerializer.h>
#include <authored/VerbBindingSet.h>
#include <authored/VerbDispatcher.h>
#include <authored/WorldVocabulary.h>
#include <core/assets/AssetRegistry.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/World.h>
#include <ui/UiService.h>
#include <ui/UiVerbBindings.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <SDL3/SDL.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

// A real click in a real document reaching a registered operation.
//
// Nothing here is synthesised past the pointer: the markup is parsed, the model
// is bound, the click is an SDL event, and the action that comes out the far
// side is the one the document raised. A test that pushed a UiAction in by hand
// would pass with the binding half wired.

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
            / ("sencha_ui_verb_test_" + std::to_string(rd()));
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

constexpr std::string_view kMarkup = R"RML(<rml>
<head><link type="text/rcss" href="panel.rcss"/></head>
<body data-model="panel">
    <div id="fire" data-event-click="panel_fire"/>
    <div id="pick" data-event-click="panel_pick(amount)"/>
    <div id="wrong" data-event-click="panel_wrong(label)"/>
</body>
</rml>)RML";

constexpr std::string_view kStyle = R"(
body { display: block; width: 100%; height: 100%; pointer-events: none; }
#fire  { display: block; position: absolute; left: 20px; top: 20px;
         width: 100px; height: 30px; pointer-events: auto; tab-index: auto; }
#pick  { display: block; position: absolute; left: 20px; top: 60px;
         width: 100px; height: 30px; pointer-events: auto; tab-index: auto; }
#wrong { display: block; position: absolute; left: 20px; top: 100px;
         width: 100px; height: 30px; pointer-events: auto; tab-index: auto; }
)";

[[nodiscard]] UiPackage MakePackage()
{
    UiPackage package;
    package.RootDocumentName = "panel.rml";

    UiPackageBlob root;
    root.VirtualName = "panel.rml";
    root.SourcePath = "ui/panel.rml";
    root.Kind = UiBlobKind::Document;
    root.Bytes = BytesOf(kMarkup);
    package.Blobs.push_back(std::move(root));

    UiPackageBlob sheet;
    sheet.VirtualName = "panel.rcss";
    sheet.SourcePath = "ui/panel.rcss";
    sheet.Kind = UiBlobKind::StyleSheet;
    sheet.Bytes = BytesOf(kStyle);
    package.Blobs.push_back(std::move(sheet));
    return package;
}

[[nodiscard]] UiScreenDesc MakeDesc()
{
    UiScreenDesc desc;
    desc.PackagePath = "asset://ui/panel.sui";
    desc.ModelName = "panel";
    desc.Properties = {
        UiModelProperty{ "amount", UiValue(std::int64_t{ 7 }), false },
        UiModelProperty{ "label", UiValue(std::string("seven")), false },
    };
    desc.Actions = { "panel_fire", "panel_pick", "panel_wrong" };
    return desc;
}

SDL_Event MouseButton(std::uint32_t type, float x, float y)
{
    SDL_Event event{};
    event.type = type;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = x;
    event.button.y = y;
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

// Records what it was asked to do, in order.
class Recorder
{
public:
    VerbAdmission Invoke(const VerbInvocation& invocation)
    {
        std::int64_t amount = 0;
        (void)invocation.Arguments->TryGetInt(0, amount);
        Calls.push_back(amount);
        return VerbAdmission::Accepted;
    }

    std::vector<std::int64_t> Calls;
};

[[nodiscard]] DataFieldSchema OneInt(std::string key)
{
    DataFieldSchema field;
    field.Key = std::move(key);
    field.Kind = DataFieldKind::Int;
    DataFieldSchema root = EmptyVerbArguments();
    root.Children.push_back(std::move(field));
    return root;
}

[[nodiscard]] VerbBindingArgument FromInput(std::string key, std::string input)
{
    VerbBindingArgument argument;
    argument.Key = std::move(key);
    argument.Source = VerbArgumentSource::Input;
    argument.Text = std::move(input);
    return argument;
}

class UiVerbFixture
{
public:
    UiVerbFixture()
        : Assets(Logging, Serializers, RuntimeAssets::ReferenceOnly{})
    {
        std::vector<std::byte> bytes;
        EXPECT_TRUE(WriteSuiToBytes(MakePackage(), bytes));
        Root.WriteBytes("ui/panel.sui", bytes);
        ScanAssetsDirectory(Root.PathString(), Assets.Registry, Assets.Assets.Kinds());
        Ui = std::make_unique<UiService>(Logging, Assets.Assets, Assets.UiPackages, Assets.Fonts,
                                         nullptr, nullptr);
        Surface = Ui->CreateSurface("test", RenderExtent{ 800, 600 });

        Verbs = &InstallVerbRegistry(Entities);
        {
            VerbRegistrationScope scope(*Verbs, "test");
            VerbDefinition fire;
            fire.Name = "test.fire";
            (void)scope.Declare(std::move(fire));
            VerbDefinition count;
            count.Name = "test.count";
            count.Arguments = OneInt("Amount");
            (void)scope.Declare(std::move(count));
            EXPECT_TRUE(scope.Commit());
        }

        VerbBindingLibrary library;
        VerbBindingDesc fire;
        fire.Key = "fire";
        fire.KeyId = MakeVerbBindingKey(fire.Key);
        fire.VerbName = "test.fire";
        library.Bindings.push_back(std::move(fire));

        VerbBindingDesc count;
        count.Key = "count";
        count.KeyId = MakeVerbBindingKey(count.Key);
        count.VerbName = "test.count";
        count.Inputs = { "amount" };
        count.Arguments.push_back(FromInput("Amount", "amount"));
        library.Bindings.push_back(std::move(count));

        std::vector<std::string> errors;
        Bindings.Instantiate(library, MakeVerbBindingEnvironment(Entities), errors);
        EXPECT_TRUE(errors.empty());

        Dispatcher = std::make_unique<VerbDispatcher>(*Verbs);
        FireToken = Dispatcher->Bind(Verbs->Find("test.fire"), Fire);
        CountToken = Dispatcher->Bind(Verbs->Find("test.count"), Count);
        Controller = std::make_unique<UiVerbBindings>(*Dispatcher, Bindings);
    }

    ~UiVerbFixture()
    {
        Controller.reset();
        if (Ui != nullptr)
            Ui->Shutdown();
    }

    [[nodiscard]] bool OpenScreenAndBind(const UiScreenDesc& desc,
                                         std::span<const UiVerbActionMapping> mappings,
                                         std::vector<std::string>& errors)
    {
        Screen = Ui->OpenScreen(Surface, desc);
        EXPECT_TRUE(Screen.IsValid());
        Ui->Update();
        return Controller->Open(Screen, desc, mappings, errors);
    }

    // What a host does every frame: drain once, hand the copied batch on.
    void PumpFrame()
    {
        Ui->Update();
        const std::vector<UiAction> batch = Ui->DrainActions(Screen);
        Controller->Dispatch(batch);
    }

    LoggingProvider Logging;
    ComponentSerializerRegistry Serializers;
    TempAssetRoot Root;
    RuntimeAssets Assets;
    std::unique_ptr<UiService> Ui;
    UiSurfaceId Surface;
    UiScreenHandle Screen;

    World Entities;
    VerbRegistry* Verbs = nullptr;
    VerbBindingSet Bindings;
    std::unique_ptr<VerbDispatcher> Dispatcher;
    Recorder Fire;
    Recorder Count;
    VerbBindingToken FireToken;
    VerbBindingToken CountToken;
    std::unique_ptr<UiVerbBindings> Controller;
};

[[nodiscard]] UiVerbActionMapping Mapping(std::string action, std::string binding)
{
    UiVerbActionMapping mapping;
    mapping.ActionName = std::move(action);
    mapping.BindingKey = std::move(binding);
    return mapping;
}

[[nodiscard]] UiVerbArgumentMapping IntArgument(std::size_t index, std::size_t slot)
{
    UiVerbArgumentMapping argument;
    argument.ArgumentIndex = index;
    argument.InputSlot = slot;
    argument.Expected = UiValueKind::Int;
    argument.Produces = VerbValueKind::Int;
    return argument;
}

[[nodiscard]] std::vector<UiVerbActionMapping> WorkingMappings()
{
    UiVerbActionMapping pick = Mapping("panel_pick", "count");
    pick.Arguments.push_back(IntArgument(0, 0));

    UiVerbActionMapping wrong = Mapping("panel_wrong", "count");
    wrong.Arguments.push_back(IntArgument(0, 0));

    return { Mapping("panel_fire", "fire"), std::move(pick), std::move(wrong) };
}
}

TEST(UiVerbBindings, AClickInTheDocumentReachesTheRegisteredOperation)
{
    UiVerbFixture fixture;
    std::vector<std::string> errors;
    ASSERT_TRUE(fixture.OpenScreenAndBind(MakeDesc(), WorkingMappings(), errors))
        << (errors.empty() ? std::string{} : errors.front());

    ClickAt(*fixture.Ui, 70.0f, 35.0f);
    fixture.PumpFrame();

    ASSERT_EQ(fixture.Fire.Calls.size(), 1u);
    EXPECT_TRUE(fixture.Count.Calls.empty());
    ASSERT_EQ(fixture.Controller->LastOutcomes().size(), 1u);
    EXPECT_EQ(fixture.Controller->LastOutcomes()[0].Status, VerbAdmission::Accepted);
    EXPECT_TRUE(fixture.Controller->LastOutcomes()[0].Id.IsValid());
}

TEST(UiVerbBindings, ThePayloadTheDocumentSentBecomesTheTypedArgument)
{
    UiVerbFixture fixture;
    std::vector<std::string> errors;
    ASSERT_TRUE(fixture.OpenScreenAndBind(MakeDesc(), WorkingMappings(), errors));

    ClickAt(*fixture.Ui, 70.0f, 75.0f);
    fixture.PumpFrame();

    ASSERT_EQ(fixture.Count.Calls.size(), 1u);
    EXPECT_EQ(fixture.Count.Calls[0], 7);
}

TEST(UiVerbBindings, APayloadOfAnotherKindIsRefusedRatherThanConverted)
{
    UiVerbFixture fixture;
    std::vector<std::string> errors;
    ASSERT_TRUE(fixture.OpenScreenAndBind(MakeDesc(), WorkingMappings(), errors));

    // The document sends the string property where the mapping declared a
    // number. Nothing here is in a position to decide that "seven" was meant
    // as a count.
    ClickAt(*fixture.Ui, 70.0f, 115.0f);
    fixture.PumpFrame();

    EXPECT_TRUE(fixture.Count.Calls.empty());
    ASSERT_EQ(fixture.Controller->LastOutcomes().size(), 1u);
    EXPECT_EQ(fixture.Controller->LastOutcomes()[0].Status, VerbAdmission::InvalidArguments);
}

TEST(UiVerbBindings, AMappingIsCheckedWhenTheScreenOpens)
{
    const auto refuses = [](std::vector<UiVerbActionMapping> mappings,
                            std::string_view expected) {
        UiVerbFixture fixture;
        std::vector<std::string> errors;
        EXPECT_FALSE(fixture.OpenScreenAndBind(MakeDesc(), mappings, errors));
        ASSERT_FALSE(errors.empty());
        bool found = false;
        for (const std::string& error : errors)
            found = found || error.find(expected) != std::string::npos;
        EXPECT_TRUE(found) << errors.front();
        EXPECT_FALSE(fixture.Controller->IsOpen());
    };

    refuses({ Mapping("panel_never_declared", "fire") }, "declares no action");
    refuses({ Mapping("panel_fire", "no_such_binding") }, "did not resolve");

    // The binding wants a value and the mapping supplies none.
    refuses({ Mapping("panel_pick", "count") }, "unfilled");

    // An argument index the binding has no slot for.
    UiVerbActionMapping overshoot = Mapping("panel_pick", "count");
    overshoot.Arguments.push_back(IntArgument(0, 3));
    refuses({ std::move(overshoot) }, "input slot");

    // An identity is opaque by construction, so no mapping can turn one into an
    // entity here.
    UiVerbActionMapping identity = Mapping("panel_pick", "count");
    UiVerbArgumentMapping fromId;
    fromId.ArgumentIndex = 0;
    fromId.InputSlot = 0;
    fromId.Expected = UiValueKind::Id;
    fromId.Produces = VerbValueKind::Entity;
    identity.Arguments.push_back(fromId);
    refuses({ std::move(identity) }, "conversion");

    // The right conversion for the wrong argument.
    UiVerbActionMapping mistyped = Mapping("panel_pick", "count");
    UiVerbArgumentMapping asText;
    asText.ArgumentIndex = 0;
    asText.InputSlot = 0;
    asText.Expected = UiValueKind::String;
    asText.Produces = VerbValueKind::String;
    mistyped.Arguments.push_back(asText);
    refuses({ std::move(mistyped) }, "wrong kind");
}

TEST(UiVerbBindings, ClosingTheScreenEndsTheAssociation)
{
    UiVerbFixture fixture;
    std::vector<std::string> errors;
    ASSERT_TRUE(fixture.OpenScreenAndBind(MakeDesc(), WorkingMappings(), errors));

    ClickAt(*fixture.Ui, 70.0f, 35.0f);
    fixture.PumpFrame();
    ASSERT_EQ(fixture.Fire.Calls.size(), 1u);

    fixture.Controller->Close();
    EXPECT_FALSE(fixture.Controller->IsOpen());

    ClickAt(*fixture.Ui, 70.0f, 35.0f);
    fixture.PumpFrame();
    EXPECT_EQ(fixture.Fire.Calls.size(), 1u)
        << "a closed controller still dispatched a screen it no longer holds";
}

TEST(UiVerbBindings, ADeclarationReorderRecompilesTheScreenLocalIds)
{
    // Screen-local ids are positions in the list one opening declared. Reopening
    // with the actions in another order must not leave the controller invoking
    // whichever action moved into that slot.
    UiVerbFixture fixture;
    UiScreenDesc reordered = MakeDesc();
    reordered.Actions = { "panel_wrong", "panel_pick", "panel_fire" };

    std::vector<std::string> errors;
    ASSERT_TRUE(fixture.OpenScreenAndBind(reordered, WorkingMappings(), errors))
        << (errors.empty() ? std::string{} : errors.front());

    ClickAt(*fixture.Ui, 70.0f, 35.0f);
    fixture.PumpFrame();

    ASSERT_EQ(fixture.Fire.Calls.size(), 1u);
    EXPECT_TRUE(fixture.Count.Calls.empty());
}

TEST(UiVerbBindings, ActionsForAnotherScreenAreLeftAlone)
{
    UiVerbFixture fixture;
    std::vector<std::string> errors;
    ASSERT_TRUE(fixture.OpenScreenAndBind(MakeDesc(), WorkingMappings(), errors));

    // A host routes one batch to several consumers. A controller that acted on
    // an action it does not own would make two screens fight over one click.
    UiAction foreign;
    foreign.Screen = UiScreenHandle{};
    foreign.Id = UiActionIdAt(0);
    fixture.Controller->Dispatch({ &foreign, 1 });

    EXPECT_TRUE(fixture.Fire.Calls.empty());
    EXPECT_TRUE(fixture.Controller->LastOutcomes().empty());
}

TEST(UiVerbBindings, ABindingReplacedUnderAnOpenScreenIsNeverRunThroughTheOldMapping)
{
    UiVerbFixture fixture;
    std::vector<std::string> errors;
    ASSERT_TRUE(fixture.OpenScreenAndBind(MakeDesc(), WorkingMappings(), errors));

    ClickAt(*fixture.Ui, 70.0f, 35.0f);
    fixture.PumpFrame();
    ASSERT_EQ(fixture.Fire.Calls.size(), 1u);

    // The set is rebuilt with a different record in the storage the old one
    // occupied. The button was mapped to "fire"; whatever now sits where
    // "fire" sat must not run.
    VerbBindingLibrary replacement;
    VerbBindingDesc count;
    count.Key = "count";
    count.KeyId = MakeVerbBindingKey(count.Key);
    count.VerbName = "test.count";
    count.Inputs = { "amount" };
    count.Arguments.push_back(FromInput("Amount", "amount"));
    replacement.Bindings.push_back(std::move(count));
    fixture.Bindings.Instantiate(replacement, MakeVerbBindingEnvironment(fixture.Entities),
                                 errors);

    ClickAt(*fixture.Ui, 70.0f, 35.0f);
    fixture.PumpFrame();
    EXPECT_EQ(fixture.Fire.Calls.size(), 1u);
    EXPECT_TRUE(fixture.Count.Calls.empty()) << "the replacement ran through the old mapping";
    // The screen stays open and says why its button does nothing.
    EXPECT_TRUE(fixture.Controller->IsOpen());
    EXPECT_FALSE(fixture.Controller->LastErrors().empty());

    // Put the record back and the same button works again, with no reopen.
    VerbBindingDesc fire;
    fire.Key = "fire";
    fire.KeyId = MakeVerbBindingKey(fire.Key);
    fire.VerbName = "test.fire";
    replacement.Bindings.push_back(std::move(fire));
    fixture.Bindings.Instantiate(replacement, MakeVerbBindingEnvironment(fixture.Entities),
                                 errors);
    ClickAt(*fixture.Ui, 70.0f, 35.0f);
    fixture.PumpFrame();
    EXPECT_EQ(fixture.Fire.Calls.size(), 2u);
    EXPECT_TRUE(fixture.Controller->LastErrors().empty());
}
