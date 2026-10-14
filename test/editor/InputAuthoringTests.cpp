#include "data/DataDocumentSet.h"
#include "data/DataProperties.h"
#include "data/DataSubtypeAuthoring.h"
#include "documents/DocumentSourceSet.h"
#include "input/InputActionSetAuthoring.h"
#include "input/InputProfileAuthoring.h"
#include "properties/PropertyTree.h"

#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <SDL3/SDL_events.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>

// Input documents read as what they are: a profile's binding is titled by what
// it binds, its action chosen among those its set declares and can drive, its
// shape chosen once and setting its slots, and a slot filled by picking a
// control or pressing one; an action set's actions are titled by their shape,
// and a change of shape takes a button's firing mode with it.
namespace
{
constexpr std::string_view kProfile = "asset://input/test.profile.sdata";
constexpr std::string_view kActions = "asset://input/test.actions.sdata";

struct InputAuthoringTest : testing::Test
{
    std::filesystem::path Root = std::filesystem::temp_directory_path()
        / ("sencha_input_authoring_" + std::to_string(std::random_device{}()));
    LoggingProvider Logging;
    ComponentSerializerRegistry Serializers;
    RuntimeAssets Assets{ Logging, Serializers };
    DocumentSourceSet Sources;
    DataDocumentStore Store{ Assets, Sources };
    DataSubtypeAuthoringRegistry Authoring;
    std::unique_ptr<DataDocumentSet> Set;
    std::unique_ptr<DataProperties> Properties;

    void SetUp() override
    {
        std::filesystem::create_directories(Root / "input");
        std::ofstream(Root / "input" / "test.actions.sdata")
            << R"({ "type": "input.actions", "version": 1, "data": { "actions": [ )"
            << R"({ "name": "jump", "type": "digital", "fire": "held" }, { "name": "move", "type": "axis2" } ] } })";
        std::ofstream(Root / "input" / "test.profile.sdata")
            << R"({ "type": "input.profile", "version": 1, "data": { "actions": "asset://input/test.actions.sdata", )"
            << R"("contexts": [ { "name": "gameplay", "priority": 100, "bindings": [ )"
            << R"({ "action": "jump", "control": "key.space" } ] } ] } })";
        (void)ScanAssetsDirectory(Root.generic_string(), Assets.Registry, Assets.Assets.Kinds());
        Set = std::make_unique<DataDocumentSet>(Store, DataDocumentSetConfig{ .ContentRoot = Root, .Subtypes = {} });
        Authoring.Register(CreateInputActionSetAuthoring());
        Authoring.Register(CreateInputProfileAuthoring());
        Properties = std::make_unique<DataProperties>(*Set, [this] { return Set->Active(); }, &Authoring);
    }

    void TearDown() override
    {
        Sources.DiscardAll();
        Properties.reset();
        Set.reset();
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }

    DataDocument& Open(std::string_view path)
    {
        std::string error;
        DataDocument* document = Set->OpenOrFocus(path, error);
        EXPECT_NE(document, nullptr) << error;
        if (IDataSubtypeAuthoring* authoring = Authoring.Find(document->Subtype()))
            authoring->Update(*document, *Set);
        return *document;
    }

    [[nodiscard]] PropertyNode Node(std::string_view key)
    {
        Nodes = Properties->Nodes();
        const std::optional<PropertyPlace> place = FindPropertyNode(Nodes, key);
        EXPECT_TRUE(place.has_value()) << key;
        return place.has_value() ? place->Node() : PropertyNode{};
    }

    PropertyEditResult Commit(std::string_view key, std::string_view value)
    {
        return Properties->BeginEdit(key) ? Properties->CommitEdit(key, value) : PropertyEditResult::Refused;
    }

    std::vector<PropertyNode> Nodes;
};

constexpr std::string_view kBinding = "$.data.contexts[0].bindings[0]";
} // namespace

TEST_F(InputAuthoringTest, ABindingsActionIsChosenAmongTheDeclaredActionsItCanDrive)
{
    (void)Open(kProfile);
    const PropertyNode action = Node(std::string(kBinding) + ".action");
    EXPECT_EQ(action.Descriptor.Kind, PropertyKind::Choice);
    EXPECT_NE(std::ranges::find(action.Descriptor.Choices, "jump"), action.Descriptor.Choices.end());
    EXPECT_EQ(std::ranges::find(action.Descriptor.Choices, "move"), action.Descriptor.Choices.end())
        << "a key cannot drive a plane";
    EXPECT_EQ(Node(std::string(kBinding) + "~shape").Value, "Single control");
}

TEST_F(InputAuthoringTest, ChoosingAShapeSetsTheSlotsItUses)
{
    const DataDocument& document = Open(kProfile);
    EXPECT_EQ(Commit(std::string(kBinding) + ".action", "move"), PropertyEditResult::Applied);
    EXPECT_EQ(Commit(std::string(kBinding) + "~shape", "Four directions"), PropertyEditResult::Applied);
    const JsonValue& binding = document.Data()->Find("contexts")->AsArray()[0].Find("bindings")->AsArray()[0];
    EXPECT_EQ(binding.Find("composite")->AsString(), "cardinal");
    EXPECT_EQ(binding.Find("control"), nullptr) << "the other shapes' slots go";
    const PropertyNode left = Node(std::string(kBinding) + ".left");
    EXPECT_EQ(left.Descriptor.Kind, PropertyKind::Asset);
    ASSERT_FALSE(left.Descriptor.Verbs.empty());
    EXPECT_EQ(left.Descriptor.Verbs.back().Id, "listen");
    const std::vector<std::string> controls = Properties->Candidates(std::string(kBinding) + ".left");
    ASSERT_FALSE(controls.empty());
    EXPECT_NE(std::ranges::find(controls, "key.w"), controls.end());
}

TEST_F(InputAuthoringTest, ASlotListenedForIsFilledByTheControlPressed)
{
    const DataDocument& document = Open(kProfile);
    const std::string slot = std::string(kBinding) + ".control";
    IDataSubtypeAuthoring* profile = Authoring.Find(document.Subtype());
    ASSERT_NE(profile, nullptr);
    EXPECT_EQ(Properties->Act(slot, "listen", {}), PropertyEditResult::Applied);
    SDL_Event press{};
    press.type = SDL_EVENT_KEY_DOWN;
    press.key.scancode = SDL_SCANCODE_E;
    EXPECT_TRUE(profile->HandlePlatformEvent(press));
    const std::optional<std::pair<std::string, std::string>> write = profile->TakeWrite(document);
    ASSERT_TRUE(write.has_value());
    EXPECT_EQ(write->first, slot);
    EXPECT_EQ(write->second, "key.e");
    EXPECT_FALSE(profile->TakeWrite(document).has_value()) << "handed over once";
}

TEST_F(InputAuthoringTest, AnActionsShapeTakesAButtonsFiringModeWithIt)
{
    const DataDocument& document = Open(kActions);
    EXPECT_EQ(Node("$.data.actions[0]").Descriptor.Label, "jump - Button, while held");
    EXPECT_EQ(Commit("$.data.actions[0].type", "axis1"), PropertyEditResult::Applied);
    const JsonValue& jump = document.Data()->Find("actions")->AsArray()[0];
    EXPECT_EQ(jump.Find("fire"), nullptr);
    Nodes = Properties->Nodes();
    EXPECT_FALSE(FindPropertyNode(Nodes, "$.data.actions[0].fire").has_value());
}
