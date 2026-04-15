#include <gtest/gtest.h>
#include <input/InputTypes.h>
#include <input/InputActionRegistry.h>
#include <input/InputConfig.h>
#include <input/InputBindingTable.h>
#include <input/InputBindingCompiler.h>
#include <input/InputBindingService.h>
#include <input/SdlInputSystem.h>
#include <input/SdlInputControlResolver.h>
#include <core/json/JsonParser.h>
#include <core/logging/LoggingProvider.h>
#include <core/system/SystemHost.h>

// =============================================================================
// Input type tests
// =============================================================================

TEST(InputTypes, DefaultActionIdIsInvalid)
{
	InputActionId id{};
	EXPECT_FALSE(static_cast<bool>(id));
	EXPECT_EQ(id.Value, InvalidActionId);
}

TEST(InputTypes, ActionIdZeroIsValid)
{
	InputActionId id{0};
	EXPECT_TRUE(static_cast<bool>(id));
}

TEST(InputTypes, ActionIdEquality)
{
	InputActionId a{0}, b{0}, c{1};
	EXPECT_EQ(a, b);
	EXPECT_NE(a, c);
}

TEST(InputTypes, ActionIdOrdering)
{
	InputActionId a{0}, b{1};
	EXPECT_LT(a, b);
}

TEST(InputTypes, UserIdAndContextIdZeroInvalid)
{
	InputUserId user{};
	InputContextId ctx{};
	EXPECT_FALSE(static_cast<bool>(user));
	EXPECT_FALSE(static_cast<bool>(ctx));
}

// =============================================================================
// Config deserialization tests
// =============================================================================

static const char* TestConfigJson = R"({
	"actions": [
		{"name": "Jump"},
		{"name": "Pause"},
		{"name": "MoveLeft"},
		{"name": "MoveRight"}
	],
	"bindings": [
		{"action": "Jump",      "device": "Keyboard", "control": "Space",  "trigger": "Pressed"},
		{"action": "Pause",     "device": "Keyboard", "control": "Escape", "trigger": "Pressed"},
		{"action": "MoveLeft",  "device": "Keyboard", "control": "A",      "trigger": "Held"},
		{"action": "MoveRight", "device": "Keyboard", "control": "D",      "trigger": "Held"}
	]
})";

namespace TestActions
{
	static constexpr std::string_view Names[] = {
		"Jump", "Pause", "MoveLeft", "MoveRight",
		"Shoot", "Aim", "Fire", "Reload",
	};
	enum Action : uint16_t { Jump, Pause, MoveLeft, MoveRight, Shoot, Aim, Fire, Reload, Count };
}

static const SdlInputControlResolver TestControlResolver;
static const InputActionRegistry TestActionRegistry{TestActions::Names};

TEST(InputConfig, DeserializeValidConfig)
{
	auto json = JsonParse(TestConfigJson);
	ASSERT_TRUE(json.has_value());

	InputCompileError error;
	auto config = DeserializeInputConfig(*json, &error);
	ASSERT_TRUE(config.has_value()) << error.Message;

	EXPECT_EQ(config->Actions.size(), 4u);
	EXPECT_EQ(config->Actions[0].Name, "Jump");
	EXPECT_EQ(config->Actions[3].Name, "MoveRight");

	EXPECT_EQ(config->Bindings.size(), 4u);
	EXPECT_EQ(config->Bindings[0].Action, "Jump");
	EXPECT_EQ(config->Bindings[0].Device, "Keyboard");
	EXPECT_EQ(config->Bindings[0].Control, "Space");
	EXPECT_EQ(config->Bindings[0].Trigger, "Pressed");
}

TEST(InputConfig, DeserializeAcceptsMissingActions)
{
	auto json = JsonParse(R"({"bindings": []})");
	ASSERT_TRUE(json.has_value());

	InputCompileError error;
	auto config = DeserializeInputConfig(*json, &error);
	ASSERT_TRUE(config.has_value()) << error.Message;
	EXPECT_TRUE(config->Actions.empty());
}

TEST(InputConfig, DeserializeRejectsMissingBindings)
{
	auto json = JsonParse(R"({"actions": []})");
	ASSERT_TRUE(json.has_value());

	InputCompileError error;
	auto config = DeserializeInputConfig(*json, &error);
	EXPECT_FALSE(config.has_value());
}

// =============================================================================
// Binding compilation tests
// =============================================================================

// SDL scancodes used in tests (match SdlInputControlResolver output):
//   Space  = SDL_SCANCODE_SPACE  = 44
//   Escape = SDL_SCANCODE_ESCAPE = 41
//   A      = SDL_SCANCODE_A      = 4
//   D      = SDL_SCANCODE_D      = 7
//   R      = SDL_SCANCODE_R      = 21
//   W      = SDL_SCANCODE_W      = 26

TEST(InputBindingCompiler, CompileProducesActionIds)
{
	auto json = JsonParse(TestConfigJson);
	ASSERT_TRUE(json.has_value());

	auto config = DeserializeInputConfig(*json);
	ASSERT_TRUE(config.has_value());

	InputCompileError error;
	auto table = CompileInputBindings(*config, TestActionRegistry, TestControlResolver, &error);
	ASSERT_TRUE(table.has_value()) << error.Message;

	EXPECT_EQ(table->ActionCount, 8);
	EXPECT_EQ(table->ActionNames[0], "Jump");
	EXPECT_EQ(table->ActionNames[3], "MoveRight");
	EXPECT_EQ(table->ActionNames[4], "Shoot");
	EXPECT_EQ(table->ActionNames[7], "Reload");
}

TEST(InputBindingCompiler, CompileDoesNotRequireActionsArray)
{
	auto json = JsonParse(R"({
		"bindings": [
			{"action": "Jump", "device": "Keyboard", "control": "Space", "trigger": "Pressed"}
		]
	})");
	auto config = DeserializeInputConfig(*json);
	ASSERT_TRUE(config.has_value());

	auto table = CompileInputBindings(*config, TestActionRegistry, TestControlResolver);
	ASSERT_TRUE(table.has_value());

	auto spaceBindings = table->GetKeyboardBindings(44);
	ASSERT_EQ(spaceBindings.size(), 1u);
	EXPECT_EQ(spaceBindings[0].Action, TestActions::Jump);
}

TEST(InputBindingCompiler, CompileUsesRegistryIdsInsteadOfConfigOrder)
{
	auto json = JsonParse(R"({
		"actions": [
			{"name": "MoveRight"},
			{"name": "MoveLeft"},
			{"name": "Pause"},
			{"name": "Jump"}
		],
		"bindings": [
			{"action": "Jump", "device": "Keyboard", "control": "Space", "trigger": "Pressed"}
		]
	})");
	auto config = DeserializeInputConfig(*json);
	ASSERT_TRUE(config.has_value());

	auto table = CompileInputBindings(*config, TestActionRegistry, TestControlResolver);
	ASSERT_TRUE(table.has_value());

	auto spaceBindings = table->GetKeyboardBindings(44);
	ASSERT_EQ(spaceBindings.size(), 1u);
	EXPECT_EQ(spaceBindings[0].Action, TestActions::Jump);
}

TEST(InputBindingCompiler, CompileProducesKeyboardBindings)
{
	auto json = JsonParse(TestConfigJson);
	auto config = DeserializeInputConfig(*json);
	auto table = CompileInputBindings(*config, TestActionRegistry, TestControlResolver);
	ASSERT_TRUE(table.has_value());

	auto spaceBindings = table->GetKeyboardBindings(44);
	ASSERT_EQ(spaceBindings.size(), 1u);
	EXPECT_EQ(spaceBindings[0].Action, TestActions::Jump);
	EXPECT_EQ(spaceBindings[0].Trigger, InputTriggerType::Pressed);

	auto escBindings = table->GetKeyboardBindings(41);
	ASSERT_EQ(escBindings.size(), 1u);
	EXPECT_EQ(escBindings[0].Action, TestActions::Pause);
	EXPECT_EQ(escBindings[0].Trigger, InputTriggerType::Pressed);

	auto aBindings = table->GetKeyboardBindings(4);
	ASSERT_EQ(aBindings.size(), 1u);
	EXPECT_EQ(aBindings[0].Action, TestActions::MoveLeft);
	EXPECT_EQ(aBindings[0].Trigger, InputTriggerType::Held);
}

TEST(InputBindingCompiler, UnboundKeyReturnsEmpty)
{
	auto json = JsonParse(TestConfigJson);
	auto config = DeserializeInputConfig(*json);
	auto table = CompileInputBindings(*config, TestActionRegistry, TestControlResolver);
	ASSERT_TRUE(table.has_value());

	auto wBindings = table->GetKeyboardBindings(26); // W, not bound
	EXPECT_TRUE(wBindings.empty());
}

TEST(InputBindingCompiler, OutOfRangeControlReturnsEmpty)
{
	auto json = JsonParse(TestConfigJson);
	auto config = DeserializeInputConfig(*json);
	auto table = CompileInputBindings(*config, TestActionRegistry, TestControlResolver);
	ASSERT_TRUE(table.has_value());

	EXPECT_TRUE(table->GetKeyboardBindings(999).empty());
	EXPECT_TRUE(table->GetMouseButtonBindings(99).empty());
}

TEST(InputBindingCompiler, RejectsUnknownAction)
{
	auto json = JsonParse(R"({
		"actions": [{"name": "Jump"}],
		"bindings": [
			{"action": "NonExistent", "device": "Keyboard", "control": "Space", "trigger": "Pressed"}
		]
	})");
	auto config = DeserializeInputConfig(*json);
	ASSERT_TRUE(config.has_value());

	InputCompileError error;
	auto table = CompileInputBindings(*config, TestActionRegistry, TestControlResolver, &error);
	EXPECT_FALSE(table.has_value());
	EXPECT_NE(error.Message.find("NonExistent"), std::string::npos);
}

TEST(InputBindingCompiler, RejectsUnknownControl)
{
	auto json = JsonParse(R"({
		"actions": [{"name": "Jump"}],
		"bindings": [
			{"action": "Jump", "device": "Keyboard", "control": "FakeKey", "trigger": "Pressed"}
		]
	})");
	auto config = DeserializeInputConfig(*json);
	auto table = CompileInputBindings(*config, TestActionRegistry, TestControlResolver);
	EXPECT_FALSE(table.has_value());
}

TEST(InputBindingCompiler, MouseBindings)
{
	auto json = JsonParse(R"({
		"actions": [{"name": "Shoot"}, {"name": "Aim"}],
		"bindings": [
			{"action": "Shoot", "device": "Mouse", "control": "Left", "trigger": "Pressed"},
			{"action": "Aim",   "device": "Mouse", "control": "Right", "trigger": "Held"}
		]
	})");
	auto config = DeserializeInputConfig(*json);
	auto table = CompileInputBindings(*config, TestActionRegistry, TestControlResolver);
	ASSERT_TRUE(table.has_value());

	auto leftBindings = table->GetMouseButtonBindings(1);
	ASSERT_EQ(leftBindings.size(), 1u);
	EXPECT_EQ(leftBindings[0].Action, TestActions::Shoot);
	EXPECT_EQ(leftBindings[0].Trigger, InputTriggerType::Pressed);

	auto rightBindings = table->GetMouseButtonBindings(3);
	ASSERT_EQ(rightBindings.size(), 1u);
	EXPECT_EQ(rightBindings[0].Action, TestActions::Aim);
	EXPECT_EQ(rightBindings[0].Trigger, InputTriggerType::Held);
}

// =============================================================================
// SdlInputSystem mapping tests
// =============================================================================

class SdlInputTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		auto json = JsonParse(TestConfigJson);
		auto config = DeserializeInputConfig(json.value());
		auto table = CompileInputBindings(config.value(), TestActionRegistry, TestControlResolver);
		BindingService.SetBindings(std::move(table.value()));

		auto& sys = Systems.AddSystem<SdlInputSystem>(SystemPhase::Input, Logging, BindingService);
		InputSys = &sys;
		Systems.Init();
	}

	void TearDown() override { Systems.Shutdown(); }

	void PressKey(uint16_t control)
	{
		InputSys->GetRawInput().Emplace(
			InputDeviceType::Keyboard, true, control, 1.0f, InputUserId{});
	}

	void ReleaseKey(uint16_t control)
	{
		InputSys->GetRawInput().Emplace(
			InputDeviceType::Keyboard, false, control, 0.0f, InputUserId{});
	}

	void RunFrame() { Systems.Update(FrameTime{}); }

	auto GetActions() { return InputSys->GetEvents().Items(); }

	InputBindingService BindingService;
	LoggingProvider Logging;
	SystemHost Systems;
	SdlInputSystem* InputSys = nullptr;
};

TEST_F(SdlInputTest, PressedTriggerEmitsStarted)
{
	PressKey(44); // Space -> Jump (Pressed trigger)
	RunFrame();

	auto actions = GetActions();
	ASSERT_EQ(actions.size(), 1u);
	EXPECT_EQ(actions[0].Action, TestActions::Jump);
	EXPECT_EQ(actions[0].Phase, InputPhase::Started);
	EXPECT_FLOAT_EQ(actions[0].Value, 1.0f);
}

TEST_F(SdlInputTest, PressedTriggerIgnoresRelease)
{
	ReleaseKey(44); // Space release with Pressed trigger
	RunFrame();

	EXPECT_TRUE(GetActions().empty());
}

TEST_F(SdlInputTest, HeldTriggerEmitsStartedOnPress)
{
	PressKey(4); // A -> MoveLeft (Held trigger)
	RunFrame();

	auto actions = GetActions();
	ASSERT_GE(actions.size(), 1u);
	EXPECT_EQ(actions[0].Action, TestActions::MoveLeft);
	EXPECT_EQ(actions[0].Phase, InputPhase::Started);
}

TEST_F(SdlInputTest, HeldTriggerEmitsPerformedEachFrame)
{
	PressKey(4); // A -> MoveLeft
	RunFrame();

	// Frame 2: raw buffer auto-cleared; held should emit Performed
	RunFrame();

	auto actions = GetActions();
	ASSERT_EQ(actions.size(), 1u);
	EXPECT_EQ(actions[0].Action, TestActions::MoveLeft);
	EXPECT_EQ(actions[0].Phase, InputPhase::Performed);
}

TEST_F(SdlInputTest, HeldTriggerEmitsCanceledOnRelease)
{
	PressKey(4); // A -> MoveLeft
	RunFrame();

	ReleaseKey(4);
	RunFrame();

	auto actions = GetActions();
	ASSERT_GE(actions.size(), 1u);

	bool foundCanceled = false;
	for (const auto& a : actions)
	{
		if (a.Action == TestActions::MoveLeft && a.Phase == InputPhase::Canceled)
		{
			foundCanceled = true;
			break;
		}
	}
	EXPECT_TRUE(foundCanceled);
}

TEST_F(SdlInputTest, HeldStopsPerformedAfterRelease)
{
	PressKey(4);
	RunFrame();

	ReleaseKey(4);
	RunFrame();

	// Frame 3: should be empty
	RunFrame();

	EXPECT_TRUE(GetActions().empty());
}

TEST_F(SdlInputTest, MultipleBindingsInSameFrame)
{
	PressKey(44); // Space -> Jump
	PressKey(41); // Escape -> Pause
	RunFrame();

	auto actions = GetActions();
	ASSERT_EQ(actions.size(), 2u);
	EXPECT_EQ(actions[0].Phase, InputPhase::Started);
	EXPECT_EQ(actions[1].Phase, InputPhase::Started);
}

TEST_F(SdlInputTest, UnboundKeyProducesNoEvents)
{
	PressKey(26); // W, not bound
	RunFrame();

	EXPECT_TRUE(GetActions().empty());
}

TEST_F(SdlInputTest, ActionQueueClearedEachFrame)
{
	PressKey(44); // Space -> Jump
	RunFrame();
	ASSERT_EQ(GetActions().size(), 1u);

	// Next frame: no input, queue should be cleared
	RunFrame();
	EXPECT_TRUE(GetActions().empty());
}

// =============================================================================
// End-to-end: JSON -> compiled bindings -> binding service lookup
// =============================================================================

TEST(InputEndToEnd, FullPipelineFromJson)
{
	const char* json = R"({
		"actions": [
			{"name": "Fire"},
			{"name": "Reload"}
		],
		"bindings": [
			{"action": "Fire",   "device": "Mouse",    "control": "Left", "trigger": "Pressed"},
			{"action": "Reload", "device": "Keyboard", "control": "R",    "trigger": "Pressed"}
		]
	})";

	auto parsed = JsonParse(json);
	ASSERT_TRUE(parsed.has_value());

	auto config = DeserializeInputConfig(*parsed);
	ASSERT_TRUE(config.has_value());

	auto table = CompileInputBindings(*config, TestActionRegistry, TestControlResolver);
	ASSERT_TRUE(table.has_value());

	InputBindingService bindingService;
	bindingService.SetBindings(std::move(*table));

	// R key (SDL_SCANCODE_R = 21) should map to Reload
	auto rBindings = bindingService.GetBindings().GetKeyboardBindings(21);
	ASSERT_EQ(rBindings.size(), 1u);
	EXPECT_EQ(rBindings[0].Action, TestActions::Reload);

	// Mouse Left (button 1) should map to Fire
	auto leftBindings = bindingService.GetBindings().GetMouseButtonBindings(1);
	ASSERT_EQ(leftBindings.size(), 1u);
	EXPECT_EQ(leftBindings[0].Action, TestActions::Fire);

	// Debug name lookup
	EXPECT_EQ(bindingService.GetActionName(TestActions::Fire), "Fire");
	EXPECT_EQ(bindingService.GetActionName(TestActions::Reload), "Reload");
	EXPECT_TRUE(bindingService.GetActionName(InputActionId{}).empty());
}
