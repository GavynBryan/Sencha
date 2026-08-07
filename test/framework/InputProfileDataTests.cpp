#include <gtest/gtest.h>

#include <assets/data/DataAssetCache.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <core/json/JsonParser.h>
#include <core/metadata/DataSchema.h>
#include <input/InputBindingCache.h>
#include <input/InputProfileData.h>

#include <SDL3/SDL.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

// Authoring rules for the two input subtypes: what a valid document compiles
// to, and which mistakes fail the load rather than producing dead controls.

namespace
{
class InputDataFixture : public ::testing::Test
{
protected:
    void SetUp() override { RegisterInputProfileData(Types, Schemas); }
    void TearDown() override { UnregisterInputProfileData(Types, Schemas); }

    // Runs a document through the same two gates the loader uses: schema
    // validation, then the subtype's compiler.
    DataAssetCompileResult Compile(std::string_view typeName, std::string_view json)
    {
        DataAssetCompileResult result;
        const std::optional<JsonValue> parsed = JsonParse(json);
        if (!parsed.has_value())
        {
            result.Error = "test document is not valid json";
            return result;
        }

        if (const DataSchema* schema = Schemas.Find(typeName); schema != nullptr)
        {
            std::vector<DataValidationError> errors;
            if (!ValidateDataAgainstSchema(*parsed, *schema, errors) && !errors.empty())
            {
                result.Error = errors.front().Path + ": " + errors.front().Message;
                return result;
            }
        }

        const DataAssetTypeRegistration* type = Types.Find(typeName);
        if (type == nullptr)
        {
            result.Error = "subtype is not registered";
            return result;
        }
        return type->Compile(*parsed);
    }

    DataAssetTypeRegistry Types;
    DataSchemaRegistry Schemas;
};

constexpr std::string_view kActionSet = R"({
    "actions": [
        { "name": "move", "type": "axis2" },
        { "name": "look", "type": "axis2" },
        { "name": "jump", "type": "digital" },
        { "name": "pause", "type": "digital", "scope": "presentation" }
    ]
})";

constexpr std::string_view kProfile = R"({
    "actions": "asset://data/input_actions.sdata",
    "contexts": [
        {
            "name": "gameplay",
            "priority": 100,
            "bindings": [
                { "action": "move", "composite": "cardinal",
                  "left": "key.a", "right": "key.d", "down": "key.s", "up": "key.w" },
                { "action": "look", "control": "mouse.delta", "scale": 0.0025 },
                { "action": "jump", "control": "key.space" }
            ]
        },
        {
            "name": "menu",
            "priority": 200,
            "bindings": [
                { "action": "pause", "control": "key.escape" }
            ]
        }
    ]
})";
}

// ---------------------------------------------------------------------------
// Action sets
// ---------------------------------------------------------------------------

TEST_F(InputDataFixture, CompilesAnActionSet)
{
    const DataAssetCompileResult result = Compile(kInputActionSetTypeName, kActionSet);
    ASSERT_TRUE(result.IsValid()) << result.Error;

    const auto* actions = static_cast<const CompiledInputActionSet*>(result.Value.get());
    ASSERT_EQ(actions->Actions.size(), 4u);
    EXPECT_EQ(actions->Actions[0].Name, "move");
    EXPECT_EQ(actions->Actions[0].Type, InputActionType::Axis2D);
    EXPECT_EQ(actions->Actions[0].Scope, InputActionScope::Simulation);
    EXPECT_EQ(actions->Actions[3].Scope, InputActionScope::Presentation);
}

TEST_F(InputDataFixture, RejectsDuplicateActionNames)
{
    const DataAssetCompileResult result = Compile(kInputActionSetTypeName, R"({
        "actions": [
            { "name": "jump", "type": "digital" },
            { "name": "jump", "type": "digital" }
        ]
    })");
    EXPECT_FALSE(result.IsValid());
    EXPECT_NE(result.Error.find("duplicates action 'jump'"), std::string::npos) << result.Error;
}

TEST_F(InputDataFixture, RejectsAnUnknownActionType)
{
    const DataAssetCompileResult result = Compile(kInputActionSetTypeName, R"({
        "actions": [ { "name": "jump", "type": "quaternion" } ]
    })");
    EXPECT_FALSE(result.IsValid());
}

TEST_F(InputDataFixture, ReadsEveryFireMode)
{
    const DataAssetCompileResult result = Compile(kInputActionSetTypeName, R"({
        "actions": [
            { "name": "shoot", "type": "digital", "fire": "pressed" },
            { "name": "throw", "type": "digital", "fire": "released" },
            { "name": "jump", "type": "digital", "fire": "held" }
        ]
    })");
    ASSERT_TRUE(result.IsValid()) << result.Error;

    const auto* actions = static_cast<const CompiledInputActionSet*>(result.Value.get());
    ASSERT_EQ(actions->Actions.size(), 3u);
    EXPECT_EQ(actions->Actions[0].Fire, InputActionFireMode::Pressed);
    EXPECT_EQ(actions->Actions[1].Fire, InputActionFireMode::Released);
    EXPECT_EQ(actions->Actions[2].Fire, InputActionFireMode::Held);
}

TEST_F(InputDataFixture, AnActionThatSaysNothingFiresOnPress)
{
    // Every action set authored before the field existed loads unchanged, and
    // a button nobody has thought about behaves the way a button usually does.
    const DataAssetCompileResult result = Compile(kInputActionSetTypeName, kActionSet);
    ASSERT_TRUE(result.IsValid()) << result.Error;

    const auto* actions = static_cast<const CompiledInputActionSet*>(result.Value.get());
    EXPECT_EQ(actions->Actions[2].Fire, InputActionFireMode::Pressed);
}

TEST_F(InputDataFixture, RejectsAnUnknownFireMode)
{
    const DataAssetCompileResult result = Compile(kInputActionSetTypeName, R"({
        "actions": [ { "name": "jump", "type": "digital", "fire": "double_tap" } ]
    })");
    EXPECT_FALSE(result.IsValid());
}

TEST_F(InputDataFixture, RejectsAFireModeOnAnAxisAction)
{
    // An axis carries a magnitude, not a moment. Accepting the field there
    // would author a knob that silently never applies.
    const DataAssetCompileResult result = Compile(kInputActionSetTypeName, R"({
        "actions": [ { "name": "move", "type": "axis2", "fire": "held" } ]
    })");
    EXPECT_FALSE(result.IsValid());
    EXPECT_NE(result.Error.find("$.data.actions[0].fire"), std::string::npos) << result.Error;
    EXPECT_NE(result.Error.find("digital"), std::string::npos) << result.Error;
}

// ---------------------------------------------------------------------------
// Profiles
// ---------------------------------------------------------------------------

TEST_F(InputDataFixture, CompilesAProfileAndDeclaresItsActionSet)
{
    const DataAssetCompileResult result = Compile(kInputProfileTypeName, kProfile);
    ASSERT_TRUE(result.IsValid()) << result.Error;

    const auto* profile = static_cast<const CompiledInputProfile*>(result.Value.get());
    EXPECT_EQ(profile->ActionSetPath, "asset://data/input_actions.sdata");
    ASSERT_EQ(profile->Contexts.size(), 2u);
    EXPECT_EQ(profile->Contexts[0].Name, "gameplay");
    EXPECT_EQ(profile->Contexts[0].Priority, 100);
    ASSERT_EQ(profile->Contexts[0].Bindings.size(), 3u);

    const AuthoredInputBinding& move = profile->Contexts[0].Bindings[0];
    EXPECT_EQ(move.Action, "move");
    EXPECT_EQ(move.Binding.Kind, InputBindingKind::Cardinal);
    EXPECT_EQ(move.Binding.Controls[kBindingPositiveY].Index, SDL_SCANCODE_W);
    EXPECT_EQ(move.Binding.Controls[kBindingNegativeX].Index, SDL_SCANCODE_A);

    const AuthoredInputBinding& look = profile->Contexts[0].Bindings[1];
    EXPECT_EQ(look.Binding.Kind, InputBindingKind::Direct);
    EXPECT_EQ(look.Binding.Controls[kBindingNegativeX].Source, InputControlSource::MouseMotion);
    EXPECT_FLOAT_EQ(look.Binding.Scale, 0.0025f);

    // The action set must be staged with the profile, or binding has nothing
    // to resolve names against.
    ASSERT_EQ(result.Dependencies.size(), 1u);
    EXPECT_EQ(result.Dependencies[0].Path, "asset://data/input_actions.sdata");
    EXPECT_EQ(result.Dependencies[0].Type, AssetType::Data);
}

TEST_F(InputDataFixture, RejectsAnUnknownControlName)
{
    const DataAssetCompileResult result = Compile(kInputProfileTypeName, R"({
        "actions": "asset://data/input_actions.sdata",
        "contexts": [ { "name": "gameplay", "priority": 1, "bindings": [
            { "action": "jump", "control": "key.spacebar" } ] } ]
    })");
    EXPECT_FALSE(result.IsValid());
    EXPECT_NE(result.Error.find("names no control"), std::string::npos) << result.Error;
}

TEST_F(InputDataFixture, RejectsDuplicateContextPriorities)
{
    const DataAssetCompileResult result = Compile(kInputProfileTypeName, R"({
        "actions": "asset://data/input_actions.sdata",
        "contexts": [
            { "name": "gameplay", "priority": 10, "bindings": [] },
            { "name": "menu", "priority": 10, "bindings": [] }
        ]
    })");
    EXPECT_FALSE(result.IsValid());
    EXPECT_NE(result.Error.find("already used"), std::string::npos) << result.Error;
}

TEST_F(InputDataFixture, RejectsDuplicateContextNames)
{
    const DataAssetCompileResult result = Compile(kInputProfileTypeName, R"({
        "actions": "asset://data/input_actions.sdata",
        "contexts": [
            { "name": "gameplay", "priority": 10, "bindings": [] },
            { "name": "gameplay", "priority": 20, "bindings": [] }
        ]
    })");
    EXPECT_FALSE(result.IsValid());
    EXPECT_NE(result.Error.find("duplicates context"), std::string::npos) << result.Error;
}

TEST_F(InputDataFixture, RejectsABindingThatIsNeitherControlNorComposite)
{
    const DataAssetCompileResult result = Compile(kInputProfileTypeName, R"({
        "actions": "asset://data/input_actions.sdata",
        "contexts": [ { "name": "gameplay", "priority": 1, "bindings": [
            { "action": "jump" } ] } ]
    })");
    EXPECT_FALSE(result.IsValid());
    EXPECT_NE(result.Error.find("exactly one of control or composite"), std::string::npos) << result.Error;
}

TEST_F(InputDataFixture, RejectsAnIncompleteComposite)
{
    const DataAssetCompileResult result = Compile(kInputProfileTypeName, R"({
        "actions": "asset://data/input_actions.sdata",
        "contexts": [ { "name": "gameplay", "priority": 1, "bindings": [
            { "action": "move", "composite": "cardinal",
              "left": "key.a", "right": "key.d", "up": "key.w" } ] } ]
    })");
    EXPECT_FALSE(result.IsValid());
    EXPECT_NE(result.Error.find("down is required"), std::string::npos) << result.Error;
}

TEST_F(InputDataFixture, RejectsANonButtonInsideAComposite)
{
    const DataAssetCompileResult result = Compile(kInputProfileTypeName, R"({
        "actions": "asset://data/input_actions.sdata",
        "contexts": [ { "name": "gameplay", "priority": 1, "bindings": [
            { "action": "move", "composite": "cardinal",
              "left": "key.a", "right": "key.d", "down": "key.s", "up": "mouse.delta" } ] } ]
    })");
    EXPECT_FALSE(result.IsValid());
    EXPECT_NE(result.Error.find("not a button"), std::string::npos) << result.Error;
}

// ---------------------------------------------------------------------------
// Binding a profile to its action set
// ---------------------------------------------------------------------------

namespace
{
// Puts both documents in a cache the way the loader would, then binds them.
class InputBindFixture : public InputDataFixture
{
protected:
    InputProfileHandle Publish(std::string_view actionSetJson, std::string_view profileJson)
    {
        const DataAssetCompileResult actions = Compile(kInputActionSetTypeName, actionSetJson);
        EXPECT_TRUE(actions.IsValid()) << actions.Error;
        (void)Cache.Register(kActionSetPath, std::string(kInputActionSetTypeName), actions.Value);

        const DataAssetCompileResult profile = Compile(kInputProfileTypeName, profileJson);
        EXPECT_TRUE(profile.IsValid()) << profile.Error;
        return InputProfileHandle{ Cache.Register(kProfilePath, std::string(kInputProfileTypeName), profile.Value) };
    }

    // The author saves an edit to one of the two documents.
    void ReloadActionSet(std::string_view actionSetJson)
    {
        const DataAssetCompileResult actions = Compile(kInputActionSetTypeName, actionSetJson);
        ASSERT_TRUE(actions.IsValid()) << actions.Error;
        ASSERT_TRUE(Cache.ReloadInPlace(kActionSetPath, kInputActionSetTypeName, actions.Value));
    }

    void ReloadProfile(std::string_view profileJson)
    {
        const DataAssetCompileResult profile = Compile(kInputProfileTypeName, profileJson);
        ASSERT_TRUE(profile.IsValid()) << profile.Error;
        ASSERT_TRUE(Cache.ReloadInPlace(kProfilePath, kInputProfileTypeName, profile.Value));
    }

    static constexpr std::string_view kActionSetPath = "asset://data/input_actions.sdata";
    static constexpr std::string_view kProfilePath = "asset://data/input_default.sdata";

    DataAssetCache Cache;
};
}

TEST_F(InputBindFixture, BindsNamesToDenseIndicesInClaimOrder)
{
    const InputProfileHandle handle = Publish(kActionSet, kProfile);
    InputBindingCache bindings(Cache);

    const BoundInputProfile* bound = bindings.Get(handle);
    const std::string error = DescribeBindErrors(bindings.Status(handle));
    ASSERT_NE(bound, nullptr) << error;

    // Highest priority first, so a resolve pass walks contexts in claim order.
    ASSERT_EQ(bound->Contexts.size(), 2u);
    EXPECT_EQ(bound->Contexts[0].Name, "menu");
    EXPECT_EQ(bound->Contexts[1].Name, "gameplay");
    EXPECT_EQ(bound->ActionCount(), 4u);

    const InputActionRegistry* actions = bindings.GetActions(handle);
    ASSERT_NE(actions, nullptr);
    const InputActionId jump = actions->Find("jump");
    ASSERT_TRUE(jump.IsValid());

    const InputContextDefinition& gameplay = bound->Contexts[1];
    const InputBinding& jumpBinding = bound->Bindings[gameplay.FirstBinding + 2];
    EXPECT_EQ(jumpBinding.ActionIndex, InputActionRegistry::IndexOf(jump));
}

TEST_F(InputBindFixture, FireModesLandBesideTheActionTypes)
{
    const InputProfileHandle handle = Publish(R"({
        "actions": [
            { "name": "jump", "type": "digital", "fire": "held" },
            { "name": "throw", "type": "digital", "fire": "released" },
            { "name": "move", "type": "axis2" }
        ]
    })", R"({
        "actions": "asset://data/input_actions.sdata",
        "contexts": [ { "name": "gameplay", "priority": 100, "bindings": [
            { "action": "jump", "control": "key.space" }
        ]} ]
    })");
    InputBindingCache bindings(Cache);

    const BoundInputProfile* bound = bindings.Get(handle);
    const InputActionRegistry* actions = bindings.GetActions(handle);
    ASSERT_NE(bound, nullptr) << DescribeBindErrors(bindings.Status(handle));
    ASSERT_NE(actions, nullptr);

    // A resolve pass indexes both tables by the same dense index without
    // checking either, so they have to stay the same length.
    ASSERT_EQ(bound->ActionFireModes.size(), bound->ActionTypes.size());
    EXPECT_EQ(bound->ActionFireModes[InputActionRegistry::IndexOf(actions->Find("jump"))],
              InputActionFireMode::Held);
    EXPECT_EQ(bound->ActionFireModes[InputActionRegistry::IndexOf(actions->Find("throw"))],
              InputActionFireMode::Released);
}

TEST_F(InputBindFixture, ARetiredSlotKeepsAFireModeItCanBeIndexedBy)
{
    const InputProfileHandle handle = Publish(R"({
        "actions": [
            { "name": "jump", "type": "digital", "fire": "held" },
            { "name": "shout", "type": "digital", "fire": "released" }
        ]
    })", R"({
        "actions": "asset://data/input_actions.sdata",
        "contexts": [ { "name": "gameplay", "priority": 100, "bindings": [
            { "action": "jump", "control": "key.space" }
        ]} ]
    })");
    InputBindingCache bindings(Cache);
    ASSERT_NE(bindings.Get(handle), nullptr);

    const InputActionId shout = bindings.GetActions(handle)->Find("shout");
    ASSERT_TRUE(shout.IsValid());

    // The author drops an action. Its slot stays so a cached id remains a
    // valid index; the mode table has to keep pace with the type table.
    ReloadActionSet(R"({
        "actions": [ { "name": "jump", "type": "digital", "fire": "held" } ]
    })");

    const BoundInputProfile* bound = bindings.Get(handle);
    ASSERT_NE(bound, nullptr) << DescribeBindErrors(bindings.Status(handle));
    ASSERT_EQ(bound->ActionFireModes.size(), bound->ActionTypes.size());
    EXPECT_GT(bound->ActionFireModes.size(), InputActionRegistry::IndexOf(shout));
}

TEST_F(InputBindFixture, ABindingThatCannotResolveCostsOnlyItself)
{
    // Refusing the whole profile over one binding left the player with no
    // controls at all -- no movement, no look, nothing -- which is a far worse
    // answer to a mistake than one dead binding. A stick cannot drive a button
    // action, so this jump binding is the one that cannot resolve.
    const InputProfileHandle handle = Publish(kActionSet, R"({
        "actions": "asset://data/input_actions.sdata",
        "contexts": [ { "name": "gameplay", "priority": 100, "bindings": [
            { "action": "move", "composite": "cardinal",
              "left": "key.a", "right": "key.d", "down": "key.s", "up": "key.w" },
            { "action": "jump", "control": "gamepad.left_stick" },
            { "action": "look", "control": "mouse.delta" } ] } ]
    })");
    InputBindingCache bindings(Cache);

    const BoundInputProfile* bound = bindings.Get(handle);
    const std::string error = DescribeBindErrors(bindings.Status(handle));
    ASSERT_NE(bound, nullptr) << "the profile still binds";

    // Move and look survive; only the trigger binding is dropped.
    ASSERT_EQ(bound->Contexts.size(), 1u);
    EXPECT_EQ(bound->Bindings.size(), 2u);
    EXPECT_EQ(bound->Contexts[0].BindingCount, 2u);
    EXPECT_NE(error.find("binding 2"), std::string::npos) << error;
    EXPECT_NE(error.find("cannot produce"), std::string::npos) << error;
}

TEST_F(InputBindFixture, ATriggerBindsToAButtonAction)
{
    // Fire and jump on a trigger is how a pad is bound; the threshold is what
    // turns the pull into a press.
    const InputProfileHandle handle = Publish(kActionSet, R"({
        "actions": "asset://data/input_actions.sdata",
        "contexts": [ { "name": "gameplay", "priority": 100, "bindings": [
            { "action": "jump", "control": "gamepad.right_trigger", "threshold": 0.4 } ] } ]
    })");
    InputBindingCache bindings(Cache);

    const BoundInputProfile* bound = bindings.Get(handle);
    const std::string error = DescribeBindErrors(bindings.Status(handle));
    ASSERT_NE(bound, nullptr) << error;
    EXPECT_TRUE(error.empty()) << error;
    ASSERT_EQ(bound->Bindings.size(), 1u);
    EXPECT_FLOAT_EQ(bound->Bindings[0].Threshold, 0.4f);
}

TEST_F(InputBindFixture, ReportsEveryBindingThatFailedNotJustTheFirst)
{
    const InputProfileHandle handle = Publish(kActionSet, R"({
        "actions": "asset://data/input_actions.sdata",
        "contexts": [ { "name": "gameplay", "priority": 100, "bindings": [
            { "action": "teleport", "control": "key.t" },
            { "action": "jump", "control": "gamepad.left_stick" } ] } ]
    })");
    InputBindingCache bindings(Cache);

    // Fixing one mistake should not be how the author discovers the next.
    ASSERT_NE(bindings.Get(handle), nullptr);
    const std::string error = DescribeBindErrors(bindings.Status(handle));
    EXPECT_NE(error.find("teleport"), std::string::npos) << error;
    EXPECT_NE(error.find("jump"), std::string::npos) << error;
}

TEST_F(InputBindFixture, RejectsABindingForAnUnknownAction)
{
    const InputProfileHandle handle = Publish(kActionSet, R"({
        "actions": "asset://data/input_actions.sdata",
        "contexts": [ { "name": "gameplay", "priority": 1, "bindings": [
            { "action": "teleport", "control": "key.t" } ] } ]
    })");
    InputBindingCache bindings(Cache);

    const BoundInputProfile* bound = bindings.Get(handle);
    const std::string error = DescribeBindErrors(bindings.Status(handle));
    ASSERT_NE(bound, nullptr);
    EXPECT_TRUE(bound->Bindings.empty()) << "the unresolvable binding is dropped";
    EXPECT_NE(error.find("names no declared action"), std::string::npos) << error;
}

TEST_F(InputBindFixture, RejectsAControlThatCannotProduceTheActionsValue)
{
    // jump is a button action; the mouse's motion is a plane, and unlike a
    // trigger it has no single value a threshold could compare.
    const InputProfileHandle handle = Publish(kActionSet, R"({
        "actions": "asset://data/input_actions.sdata",
        "contexts": [ { "name": "gameplay", "priority": 1, "bindings": [
            { "action": "jump", "control": "mouse.delta" } ] } ]
    })");
    InputBindingCache bindings(Cache);

    const BoundInputProfile* bound = bindings.Get(handle);
    const std::string error = DescribeBindErrors(bindings.Status(handle));
    ASSERT_NE(bound, nullptr);
    EXPECT_TRUE(bound->Bindings.empty()) << "the unresolvable binding is dropped";
    EXPECT_NE(error.find("cannot produce"), std::string::npos) << error;
}

TEST_F(InputBindFixture, ReportsAMissingActionSet)
{
    const DataAssetCompileResult profile = Compile(kInputProfileTypeName, kProfile);
    ASSERT_TRUE(profile.IsValid()) << profile.Error;
    const InputProfileHandle handle{
        Cache.Register(kProfilePath, std::string(kInputProfileTypeName), profile.Value) };

    InputBindingCache bindings(Cache);
    EXPECT_EQ(bindings.Get(handle), nullptr)
        << "without a vocabulary nothing can bind";

    const InputBindStatus status = bindings.Status(handle);
    EXPECT_EQ(status.State, InputBindState::Failed)
        << "nothing bound before, so there is nothing to fall back on";
    EXPECT_NE(DescribeBindErrors(status).find("is missing"), std::string::npos);
}

TEST_F(InputBindFixture, RebindsWhenTheProfileReloadsAndKeepsTheHandle)
{
    const InputProfileHandle handle = Publish(kActionSet, kProfile);
    InputBindingCache bindings(Cache);
    ASSERT_NE(bindings.Get(handle), nullptr);
    const std::uint64_t rebuildsAfterFirstBind = bindings.RebuildCount();

    const DataAssetCompileResult edited = Compile(kInputProfileTypeName, R"({
        "actions": "asset://data/input_actions.sdata",
        "contexts": [ { "name": "gameplay", "priority": 100, "bindings": [
            { "action": "jump", "control": "key.j" } ] } ]
    })");
    ASSERT_TRUE(edited.IsValid()) << edited.Error;
    ASSERT_TRUE(Cache.ReloadInPlace(kProfilePath, kInputProfileTypeName, edited.Value));

    const BoundInputProfile* bound = bindings.Get(handle);
    ASSERT_NE(bound, nullptr);
    EXPECT_GT(bindings.RebuildCount(), rebuildsAfterFirstBind);
    ASSERT_EQ(bound->Contexts.size(), 1u);
    ASSERT_EQ(bound->Bindings.size(), 1u);
    EXPECT_EQ(bound->Bindings[0].Controls[kBindingNegativeX].Index, SDL_SCANCODE_J);
}

// ---------------------------------------------------------------------------
// The template's shipped controls
// ---------------------------------------------------------------------------

namespace
{
// Reads a committed asset out of the source tree. The template's controls are
// content, not fixtures: if they stop loading, a new project made from the
// template starts with no controls at all.
std::string ReadRepoFile(std::string_view relativePath)
{
    const std::filesystem::path path =
        std::filesystem::path(SENCHA_REPO_ROOT) / relativePath;
    std::ifstream file(path);
    if (!file)
        return {};
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

// Unwraps the {type, version, data} envelope the loader checks before handing
// `data` to the subtype's compiler.
JsonValue DataSection(const std::string& document, std::string_view expectedType)
{
    const std::optional<JsonValue> parsed = JsonParse(document);
    EXPECT_TRUE(parsed.has_value()) << "document is not valid json";
    if (!parsed.has_value())
        return {};

    const JsonValue* type = parsed->Find("type");
    EXPECT_NE(type, nullptr);
    if (type != nullptr)
    {
        EXPECT_EQ(type->AsString(), expectedType);
    }

    const JsonValue* version = parsed->Find("version");
    EXPECT_NE(version, nullptr);

    const JsonValue* data = parsed->Find("data");
    EXPECT_NE(data, nullptr);
    return data != nullptr ? *data : JsonValue{};
}
}

TEST_F(InputBindFixture, TheTemplatesShippedControlsLoadAndBind)
{
    const std::string actionsDocument =
        ReadRepoFile("template/assets/data/input_actions.sdata");
    const std::string profileDocument =
        ReadRepoFile("template/assets/data/input_default.sdata");
    ASSERT_FALSE(actionsDocument.empty());
    ASSERT_FALSE(profileDocument.empty());

    const JsonValue actionsData = DataSection(actionsDocument, kInputActionSetTypeName);
    const JsonValue profileData = DataSection(profileDocument, kInputProfileTypeName);

    std::vector<DataValidationError> errors;
    const DataSchema* actionSchema = Schemas.Find(kInputActionSetTypeName);
    ASSERT_NE(actionSchema, nullptr);
    EXPECT_TRUE(ValidateDataAgainstSchema(actionsData, *actionSchema, errors))
        << (errors.empty() ? "" : errors.front().Path + ": " + errors.front().Message);

    errors.clear();
    const DataSchema* profileSchema = Schemas.Find(kInputProfileTypeName);
    ASSERT_NE(profileSchema, nullptr);
    EXPECT_TRUE(ValidateDataAgainstSchema(profileData, *profileSchema, errors))
        << (errors.empty() ? "" : errors.front().Path + ": " + errors.front().Message);

    DataAssetCompileResult actions = Types.Find(kInputActionSetTypeName)->Compile(actionsData);
    ASSERT_TRUE(actions.IsValid()) << actions.Error;
    (void)Cache.Register(kActionSetPath, std::string(kInputActionSetTypeName), actions.Value);

    DataAssetCompileResult profile = Types.Find(kInputProfileTypeName)->Compile(profileData);
    ASSERT_TRUE(profile.IsValid()) << profile.Error;
    const InputProfileHandle handle{ Cache.Register(
        kProfilePath, std::string(kInputProfileTypeName), profile.Value) };

    InputBindingCache bindings(Cache);
    const BoundInputProfile* bound = bindings.Get(handle);
    ASSERT_NE(bound, nullptr) << DescribeBindErrors(bindings.Status(handle));

    // The actions the template's systems resolve by name at startup.
    const InputActionRegistry* registry = bindings.GetActions(handle);
    ASSERT_NE(registry, nullptr);
    EXPECT_TRUE(registry->Find("move").IsValid());
    EXPECT_TRUE(registry->Find("look").IsValid());
    EXPECT_TRUE(registry->Find("jump").IsValid());

    // And the context the template holds a lease on for its whole run.
    EXPECT_NE(bound->FindContext("gameplay"), kInvalidInputContext);
}

TEST_F(InputBindFixture, ABadReloadReportsWhatItDroppedAndBindsTheRest)
{
    const InputProfileHandle handle = Publish(kActionSet, kProfile);
    InputBindingCache bindings(Cache);
    ASSERT_NE(bindings.Get(handle), nullptr);

    ReloadProfile(R"({
        "actions": "asset://data/input_actions.sdata",
        "contexts": [ { "name": "gameplay", "priority": 100, "bindings": [
            { "action": "nonexistent", "control": "key.j" } ] } ]
    })");

    const BoundInputProfile* bound = bindings.Get(handle);
    const std::string error = DescribeBindErrors(bindings.Status(handle));

    // The edit is reported, and what still resolves still binds.
    EXPECT_NE(error.find("names no declared action"), std::string::npos) << error;
    ASSERT_NE(bound, nullptr);
    EXPECT_TRUE(bound->Bindings.empty());
    EXPECT_EQ(bindings.Status(handle).State, InputBindState::Current)
        << "a profile that compiled is current, however little of it resolved";
}

TEST_F(InputBindFixture, ARebindWithNoVocabularyKeepsTheLastGoodTables)
{
    const InputProfileHandle handle = Publish(kActionSet, kProfile);
    InputBindingCache bindings(Cache);
    const BoundInputProfile* first = bindings.Get(handle);
    ASSERT_NE(first, nullptr);
    const std::size_t boundBefore = first->Bindings.size();
    ASSERT_GT(boundBefore, 0u);

    // The profile is edited to name an action set nothing published, so not one
    // binding in it can resolve and there are no tables to produce at all.
    ReloadProfile(R"({
        "actions": "asset://data/input_absent.sdata",
        "contexts": [ { "name": "gameplay", "priority": 100, "bindings": [
            { "action": "jump", "control": "key.space" } ] } ]
    })");

    const BoundInputProfile* bound = bindings.Get(handle);
    ASSERT_NE(bound, nullptr) << "the player keeps the controls that were working";
    EXPECT_EQ(bound->Bindings.size(), boundBefore);

    const InputBindStatus status = bindings.Status(handle);
    EXPECT_EQ(status.State, InputBindState::Stale);
    EXPECT_NE(DescribeBindErrors(status).find("is missing"), std::string::npos);
}

TEST_F(InputBindFixture, ReorderingTheActionSetDoesNotRepointCachedIds)
{
    const InputProfileHandle handle = Publish(kActionSet, kProfile);
    InputBindingCache bindings(Cache);
    const InputActionRegistry* actions = bindings.GetActions(handle);
    ASSERT_NE(actions, nullptr);

    // What a game resolves once at startup and indexes by from then on.
    const InputActionId move = actions->Find("move");
    const InputActionId jump = actions->Find("jump");
    ASSERT_TRUE(move.IsValid());
    ASSERT_TRUE(jump.IsValid());

    // The author drags jump to the top of the document and saves.
    ReloadActionSet(R"({
        "actions": [
            { "name": "jump", "type": "digital" },
            { "name": "move", "type": "axis2" },
            { "name": "look", "type": "axis2" },
            { "name": "pause", "type": "digital", "scope": "presentation" }
        ]
    })");

    const InputActionRegistry* rebound = bindings.GetActions(handle);
    ASSERT_NE(rebound, nullptr);
    EXPECT_EQ(rebound->Find("move"), move) << "a cached id still means the action it was resolved for";
    EXPECT_EQ(rebound->Find("jump"), jump);

    // And the recompiled bindings drive those same slots, so the player's keys
    // still do what the document says they do.
    const BoundInputProfile* bound = bindings.Get(handle);
    ASSERT_NE(bound, nullptr);
    EXPECT_EQ(bound->ActionTypes[InputActionRegistry::IndexOf(move)], InputActionType::Axis2D);
    EXPECT_EQ(bound->ActionTypes[InputActionRegistry::IndexOf(jump)], InputActionType::Digital);

    const std::uint32_t gameplay = bound->FindContext("gameplay");
    ASSERT_NE(gameplay, kInvalidInputContext);
    const InputBinding& jumpBinding =
        bound->Bindings[bound->Contexts[gameplay].FirstBinding + 2];
    EXPECT_EQ(jumpBinding.ActionIndex, InputActionRegistry::IndexOf(jump));
    EXPECT_EQ(jumpBinding.Controls[kBindingNegativeX].Index, SDL_SCANCODE_SPACE);
}

TEST_F(InputBindFixture, RetargetingTheProfilesActionSetBindsAgainstTheNewOne)
{
    const InputProfileHandle handle = Publish(kActionSet, kProfile);
    InputBindingCache bindings(Cache);
    ASSERT_NE(bindings.Get(handle), nullptr);

    // A second vocabulary, the way a project grows one for a separate mode.
    const DataAssetCompileResult other = Compile(kInputActionSetTypeName, R"({
        "actions": [ { "name": "fire", "type": "digital" } ]
    })");
    ASSERT_TRUE(other.IsValid()) << other.Error;
    (void)Cache.Register("asset://data/input_vehicle.sdata",
                         std::string(kInputActionSetTypeName), other.Value);

    ReloadProfile(R"({
        "actions": "asset://data/input_vehicle.sdata",
        "contexts": [ { "name": "gameplay", "priority": 100, "bindings": [
            { "action": "fire", "control": "key.space" } ] } ]
    })");

    // The lease and its reload version follow the path the profile names; a
    // lease taken once and kept would still be watching the old document.
    const InputActionRegistry* actions = bindings.GetActions(handle);
    ASSERT_NE(actions, nullptr);
    EXPECT_TRUE(actions->Find("fire").IsValid());
    EXPECT_FALSE(actions->Find("jump").IsValid());

    const BoundInputProfile* bound = bindings.Get(handle);
    ASSERT_NE(bound, nullptr);
    EXPECT_EQ(bound->Bindings.size(), 1u);
    EXPECT_TRUE(DescribeBindErrors(bindings.Status(handle)).empty());
}

TEST_F(InputBindFixture, DiagnosticsAreNotConsumedByWhoeverReadsThemFirst)
{
    // A game asks once at startup and a reporter asks every frame. Handing the
    // failure to whichever got there first is how a dropped binding stays
    // silent, since the profile it was dropped from binds fine without it.
    const InputProfileHandle handle = Publish(kActionSet, R"({
        "actions": "asset://data/input_actions.sdata",
        "contexts": [ { "name": "gameplay", "priority": 100, "bindings": [
            { "action": "move", "composite": "cardinal",
              "left": "key.a", "right": "key.d", "down": "key.s", "up": "key.w" },
            { "action": "teleport", "control": "key.t" } ] } ]
    })");
    InputBindingCache bindings(Cache);
    ASSERT_NE(bindings.GetActions(handle), nullptr) << "the rest of the profile still binds";

    const InputBindStatus startup = bindings.Status(handle);
    ASSERT_FALSE(startup.Errors.empty());

    const InputBindStatus reporter = bindings.Status(handle);
    EXPECT_EQ(DescribeBindErrors(reporter), DescribeBindErrors(startup));
    EXPECT_EQ(reporter.Revision, startup.Revision)
        << "an unchanged failure is not a new one to report";
}
