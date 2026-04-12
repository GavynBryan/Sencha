#include <gtest/gtest.h>
#include <input/InputTypes.h>
#include <input/InputActionRegistry.h>
#include <input/InputConfig.h>
#include <input/InputBindingTable.h>
#include <input/InputBindingCompiler.h>
#include <input/RawInputBufferService.h>
#include <input/InputBindingService.h>
#include <input/InputEventQueueService.h>
#include <input/InputStateService.h>
#include <input/InputMappingSystem.h>
#include <json/JsonParser.h>
#include <logging/LoggingProvider.h>
#include <system/SystemHost.h>

// =============================================================================
// Input type tests
// =============================================================================

TEST(InputTypes, ActionIdZeroIsInvalid)
{
	InputActionId id{};
	EXPECT_FALSE(static_cast<bool>(id));
	EXPECT_EQ(id.Value, 0);
}

TEST(InputTypes, ActionIdNonZeroIsValid)
{
	InputActionId id{1};
	EXPECT_TRUE(static_cast<bool>(id));
}

TEST(InputTypes, ActionIdEquality)
{
	InputActionId a{1}, b{1}, c{2};
	EXPECT_EQ(a, b);
	EXPECT_NE(a, c);
}

TEST(InputTypes, ActionIdOrdering)
{
	InputActionId a{1}, b{2};
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

class TestInputControlResolver final : public IInputControlResolver
{
public:
	[[nodiscard]] std::optional<uint16_t> ResolveControl(
		InputDeviceType device,
		std::string_view control) const override
	{
		switch (device)
		{
		case InputDeviceType::Keyboard:
			if (control == "A") return 4;
			if (control == "D") return 7;
			if (control == "R") return 21;
			if (control == "W") return 26;
			if (control == "Escape") return 41;
			if (control == "Space") return 44;
			return std::nullopt;
		case InputDeviceType::Mouse:
			if (control == "Left") return MouseControl::Left;
			if (control == "Right") return MouseControl::Right;
			return std::nullopt;
		default:
			return std::nullopt;
		}
	}
};

namespace TestActions
{
	constexpr InputActionId Jump{1};
	constexpr InputActionId Pause{2};
	constexpr InputActionId MoveLeft{3};
	constexpr InputActionId MoveRight{4};
	constexpr InputActionId Shoot{5};
	constexpr InputActionId Aim{6};
	constexpr InputActionId Fire{7};
	constexpr InputActionId Reload{8};

	static constexpr InputActionEntry Entries[] = {
		{"Jump", Jump},
		{"Pause", Pause},
		{"MoveLeft", MoveLeft},
		{"MoveRight", MoveRight},
		{"Shoot", Shoot},
		{"Aim", Aim},
		{"Fire", Fire},
		{"Reload", Reload},
	};
}

static const TestInputControlResolver TestControlResolver;
static const InputActionRegistry TestActionRegistry{TestActions::Entries};

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

	// Space = test control code 44
	auto spaceBindings = table->GetKeyboardBindings(44);
	ASSERT_EQ(spaceBindings.size(), 1u);
	EXPECT_EQ(spaceBindings[0].Action, TestActions::Jump);
	EXPECT_EQ(spaceBindings[0].Trigger, InputTriggerType::Pressed);

	// Escape = test control code 41
	auto escBindings = table->GetKeyboardBindings(41);
	ASSERT_EQ(escBindings.size(), 1u);
	EXPECT_EQ(escBindings[0].Action, TestActions::Pause);
	EXPECT_EQ(escBindings[0].Trigger, InputTriggerType::Pressed);

	// A = test control code 4
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

	// W = test control code 26, not bound
	auto wBindings = table->GetKeyboardBindings(26);
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

	auto leftBindings = table->GetMouseButtonBindings(1); // Left = 1
	ASSERT_EQ(leftBindings.size(), 1u);
	EXPECT_EQ(leftBindings[0].Action, TestActions::Shoot);
	EXPECT_EQ(leftBindings[0].Trigger, InputTriggerType::Pressed);

	auto rightBindings = table->GetMouseButtonBindings(3); // Right = 3
	ASSERT_EQ(rightBindings.size(), 1u);
	EXPECT_EQ(rightBindings[0].Action, TestActions::Aim);
	EXPECT_EQ(rightBindings[0].Trigger, InputTriggerType::Held);
}

// =============================================================================
// InputMappingSystem tests
// =============================================================================

class InputMappingTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		auto json = JsonParse(TestConfigJson);
		auto config = DeserializeInputConfig(json.value());
		auto table = CompileInputBindings(config.value(), TestActionRegistry, TestControlResolver);
		BindingService.SetBindings(std::move(table.value()));

		auto& logger = Logging.GetLogger<InputMappingSystem>();
		Systems.AddSystem<InputMappingSystem>(0,
			logger, RawBuffer, BindingService, ActionQueue, &StateService);
		Systems.Init();
	}

	void TearDown() override
	{
		Systems.Shutdown();
	}

	void PressKey(uint16_t control)
	{
		RawBuffer.GetBuffer().Emplace(
			InputDeviceType::Keyboard, true, control, 1.0f, InputUserId{});
	}

	void ReleaseKey(uint16_t control)
	{
		RawBuffer.GetBuffer().Emplace(
			InputDeviceType::Keyboard, false, control, 0.0f, InputUserId{});
	}

	void RunMapping()
	{
		Systems.Update();
	}

	auto GetActions() { return ActionQueue.GetBuffer().Items(); }

	RawInputBufferService RawBuffer;
	InputBindingService BindingService;
	InputEventQueueService ActionQueue;
	InputStateService StateService;
	LoggingProvider Logging;
	SystemHost Systems;
};

TEST_F(InputMappingTest, PressedTriggerEmitsStarted)
{
	PressKey(44); // Space -> Jump (Pressed trigger)
	RunMapping();

	auto actions = GetActions();
	ASSERT_EQ(actions.size(), 1u);
	EXPECT_EQ(actions[0].Action, TestActions::Jump);
	EXPECT_EQ(actions[0].Phase, InputPhase::Started);
	EXPECT_FLOAT_EQ(actions[0].Value, 1.0f);
}

TEST_F(InputMappingTest, PressedTriggerIgnoresRelease)
{
	ReleaseKey(44); // Space release with Pressed trigger
	RunMapping();

	auto actions = GetActions();
	EXPECT_TRUE(actions.empty());
}

TEST_F(InputMappingTest, HeldTriggerEmitsStartedOnPress)
{
	PressKey(4); // A -> MoveLeft (Held trigger)
	RunMapping();

	auto actions = GetActions();
	ASSERT_GE(actions.size(), 1u);
	EXPECT_EQ(actions[0].Action, TestActions::MoveLeft);
	EXPECT_EQ(actions[0].Phase, InputPhase::Started);
}

TEST_F(InputMappingTest, HeldTriggerEmitsPerformedEachFrame)
{
	// Frame 1: press
	PressKey(4); // A -> MoveLeft
	RunMapping();
	RawBuffer.GetBuffer().Clear();

	// Frame 2: no new raw events, but held should emit Performed
	RunMapping();

	auto actions = GetActions();
	ASSERT_EQ(actions.size(), 1u);
	EXPECT_EQ(actions[0].Action, TestActions::MoveLeft);
	EXPECT_EQ(actions[0].Phase, InputPhase::Performed);
}

TEST_F(InputMappingTest, HeldTriggerEmitsCanceledOnRelease)
{
	// Frame 1: press
	PressKey(4); // A -> MoveLeft
	RunMapping();
	RawBuffer.GetBuffer().Clear();

	// Frame 2: release
	ReleaseKey(4);
	RunMapping();

	auto actions = GetActions();
	ASSERT_GE(actions.size(), 1u);

	// Find the Canceled event
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

TEST_F(InputMappingTest, HeldStopsPerformedAfterRelease)
{
	// Frame 1: press A
	PressKey(4);
	RunMapping();
	RawBuffer.GetBuffer().Clear();

	// Frame 2: release A
	ReleaseKey(4);
	RunMapping();
	RawBuffer.GetBuffer().Clear();

	// Frame 3: should be empty (held was released)
	RunMapping();

	auto actions = GetActions();
	EXPECT_TRUE(actions.empty());
}

TEST_F(InputMappingTest, MultipleBindingsInSameFrame)
{
	PressKey(44); // Space -> Jump
	PressKey(41); // Escape -> Pause
	RunMapping();

	auto actions = GetActions();
	ASSERT_EQ(actions.size(), 2u);
	// Both should be Started
	EXPECT_EQ(actions[0].Phase, InputPhase::Started);
	EXPECT_EQ(actions[1].Phase, InputPhase::Started);
}

TEST_F(InputMappingTest, UnboundKeyProducesNoEvents)
{
	PressKey(26); // W, not bound
	RunMapping();

	EXPECT_TRUE(GetActions().empty());
}

TEST_F(InputMappingTest, StateServiceUpdatedOnPress)
{
	PressKey(44); // Space -> Jump
	RunMapping();

	EXPECT_TRUE(StateService.IsActive(InputActionId{1}));
	EXPECT_FLOAT_EQ(StateService.GetValue(InputActionId{1}), 1.0f);
}

TEST_F(InputMappingTest, StateServiceUpdatedOnHeldRelease)
{
	// Press A -> MoveLeft (Held)
	PressKey(4);
	RunMapping();
	EXPECT_TRUE(StateService.IsActive(InputActionId{3}));

	RawBuffer.GetBuffer().Clear();

	// Release A
	ReleaseKey(4);
	RunMapping();
	EXPECT_FALSE(StateService.IsActive(InputActionId{3}));
}

TEST_F(InputMappingTest, ActionQueueClearedEachFrame)
{
	PressKey(44); // Space -> Jump
	RunMapping();
	ASSERT_EQ(GetActions().size(), 1u);

	RawBuffer.GetBuffer().Clear();

	// Next frame: no input, queue should be cleared
	RunMapping();
	EXPECT_TRUE(GetActions().empty());
}

// =============================================================================
// InputStateService tests
// =============================================================================

TEST(InputStateService, DefaultStateIsInactive)
{
	InputStateService service;
	auto state = service.GetActionState(InputActionId{1});
	EXPECT_FALSE(state.Active);
	EXPECT_FLOAT_EQ(state.Value, 0.0f);
}

TEST(InputStateService, SetAndGetState)
{
	InputStateService service;
	service.SetActionState(InputActionId{1}, true, 0.75f);

	EXPECT_TRUE(service.IsActive(InputActionId{1}));
	EXPECT_FLOAT_EQ(service.GetValue(InputActionId{1}), 0.75f);
}

TEST(InputStateService, ZeroActionIdIgnored)
{
	InputStateService service;
	service.SetActionState(InputActionId{0}, true, 1.0f);
	EXPECT_FALSE(service.IsActive(InputActionId{0}));
}

TEST(InputStateService, MaxActionIdSupported)
{
	InputStateService service;
	InputActionId max{255};
	service.SetActionState(max, true, 1.0f);
	EXPECT_TRUE(service.IsActive(max));
	EXPECT_FLOAT_EQ(service.GetValue(max), 1.0f);
}

// =============================================================================
// End-to-end: JSON -> compiled bindings -> mapping
// =============================================================================

TEST(InputEndToEnd, FullPipelineFromJson)
{
	const char* json = R"({
		"actions": [
			{"name": "Fire"},
			{"name": "Reload"}
		],
		"bindings": [
			{"action": "Fire",   "device": "Mouse", "control": "Left",  "trigger": "Pressed"},
			{"action": "Reload", "device": "Keyboard", "control": "R",  "trigger": "Pressed"}
		]
	})";

	// Parse JSON
	auto parsed = JsonParse(json);
	ASSERT_TRUE(parsed.has_value());

	// Deserialize config
	auto config = DeserializeInputConfig(*parsed);
	ASSERT_TRUE(config.has_value());

	// Compile bindings
	auto table = CompileInputBindings(*config, TestActionRegistry, TestControlResolver);
	ASSERT_TRUE(table.has_value());

	// Set up services
	RawInputBufferService rawBuffer;
	InputBindingService bindingService;
	bindingService.SetBindings(std::move(*table));
	InputEventQueueService actionQueue;

	// Verify: R key (test control code 21) should map to Reload
	auto rBindings = bindingService.GetBindings().GetKeyboardBindings(21);
	ASSERT_EQ(rBindings.size(), 1u);
	EXPECT_EQ(rBindings[0].Action, TestActions::Reload);

	// Verify: Mouse Left (button 1) should map to Fire
	auto leftBindings = bindingService.GetBindings().GetMouseButtonBindings(1);
	ASSERT_EQ(leftBindings.size(), 1u);
	EXPECT_EQ(leftBindings[0].Action, TestActions::Fire);

	// Verify debug name lookup
	EXPECT_EQ(bindingService.GetActionName(TestActions::Fire), "Fire");
	EXPECT_EQ(bindingService.GetActionName(TestActions::Reload), "Reload");
	EXPECT_TRUE(bindingService.GetActionName(InputActionId{0}).empty());
}
