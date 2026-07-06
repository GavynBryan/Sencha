#include "ScriptTestSupport.h"

#include "script/ScriptBytecodeValidator.h"
#include "script/ScriptCompiler.h"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>

namespace
{
    [[nodiscard]] bool ReadFixture(std::string_view name, std::string& out)
    {
        std::ifstream file(std::string(SCRIPT_FIXTURE_DIR "/") + std::string(name));
        if (!file)
        {
            return false;
        }
        std::ostringstream text;
        text << file.rdbuf();
        out = text.str();
        return true;
    }

    // Resolver over the fixture directory: import paths are relative to the
    // importing file, and all fixtures share one directory.
    [[nodiscard]] ScriptSourceResolver FixtureResolver()
    {
        return [](std::string_view path, std::string& out) {
            return ReadFixture(path, out);
        };
    }

    [[nodiscard]] ScriptCompileResult CompileFixture(std::string_view name)
    {
        std::string source;
        EXPECT_TRUE(ReadFixture(name, source)) << name;
        return CompileScript(name, source, FixtureResolver());
    }

    [[nodiscard]] ScriptCompileResult CompileSource(std::string_view source)
    {
        return CompileScript("test.t", source, {});
    }

    void ExpectError(std::string_view source, std::string_view fragment, uint32_t line = 0)
    {
        const ScriptCompileResult result = CompileSource(source);
        EXPECT_FALSE(result.Ok) << "expected failure containing '" << fragment << "'";
        if (!result.Ok)
        {
            EXPECT_NE(result.Error.Message.find(fragment), std::string::npos)
                << "actual: " << result.Error.Message;
            if (line != 0)
            {
                EXPECT_EQ(result.Error.Line, line) << result.Error.Message;
            }
        }
    }

    [[nodiscard]] int32_t FindFunction(const ScriptModule& module, std::string_view name)
    {
        for (uint32_t i = 0; i < module.Functions.size(); ++i)
        {
            if (module.GetString(module.Functions[i].Name) == name)
            {
                return static_cast<int32_t>(i);
            }
        }
        return -1;
    }

    [[nodiscard]] int32_t FindFieldBind(const ScriptModule& module, std::string_view component,
                                        std::string_view path)
    {
        for (uint32_t i = 0; i < module.FieldBinds.size(); ++i)
        {
            if (module.GetString(module.FieldBinds[i].ComponentName) == component
                && module.GetString(module.FieldBinds[i].FieldPath) == path)
            {
                return static_cast<int32_t>(i);
            }
        }
        return -1;
    }
}

TEST(ScriptCompiler, FixturesCompileAndValidate)
{
    for (const std::string_view fixture :
         {"hookshot.t", "door.t", "pickup.t", "burn_field.t", "lib_motion.t"})
    {
        const ScriptCompileResult result = CompileFixture(fixture);
        ASSERT_TRUE(result.Ok) << fixture << ": " << result.Error.File << ":"
                               << result.Error.Line << ":" << result.Error.Col << ": "
                               << result.Error.Message;
        const ScriptValidationResult validated = ValidateScriptModule(result.Module);
        EXPECT_TRUE(validated.Ok) << fixture << ": " << validated.Rule << " "
                                  << validated.Error << " fn=" << validated.FunctionIndex
                                  << " at=" << validated.CodeOffset;

        // The container round-trips and still validates.
        const std::vector<std::byte> bytes = WriteScriptModule(result.Module);
        const ScriptModuleParseResult parsed =
            ParseScriptModule(std::span<const std::byte>(bytes));
        ASSERT_TRUE(parsed.Ok) << fixture << ": " << parsed.Error;
        EXPECT_TRUE(ValidateScriptModule(parsed.Module).Ok) << fixture;
    }
}

TEST(ScriptCompiler, CompilationIsDeterministic)
{
    const ScriptCompileResult a = CompileFixture("hookshot.t");
    const ScriptCompileResult b = CompileFixture("hookshot.t");
    ASSERT_TRUE(a.Ok && b.Ok);
    EXPECT_EQ(WriteScriptModule(a.Module), WriteScriptModule(b.Module));
}

TEST(ScriptCompiler, HookshotModuleShape)
{
    const ScriptCompileResult result = CompileFixture("hookshot.t");
    ASSERT_TRUE(result.Ok) << result.Error.Message;
    const ScriptModule& m = result.Module;

    ASSERT_EQ(m.Declarations.size(), 1u);
    EXPECT_EQ(m.Declarations[0].Kind, ScriptDeclKind::Ability);
    EXPECT_EQ(m.GetString(m.Declarations[0].Name), "Hookshot");
    ASSERT_EQ(m.Declarations[0].States.size(), 1u);
    EXPECT_EQ(m.GetString(m.Declarations[0].States[0]), "Pulling");
    EXPECT_EQ(m.Declarations[0].Callbacks.size(), 3u);

    // Script component with flattened leaves.
    ASSERT_EQ(m.Components.size(), 1u);
    EXPECT_EQ(m.GetString(m.Components[0].Name), "HookshotState");
    ASSERT_EQ(m.Components[0].Fields.size(), 5u); // target.xyz, target_entity, pulling
    EXPECT_EQ(m.GetString(m.Components[0].Fields[0].Name), "target.x");
    EXPECT_EQ(m.Components[0].Fields[3].Scalar,
              static_cast<uint8_t>(ScriptScalarKind::Entity));

    // Params in declaration order.
    ASSERT_EQ(m.Params.size(), 3u);
    EXPECT_EQ(m.GetString(m.Params[0].Name), "range");
    EXPECT_EQ(m.Params[0].DefaultRaw[0], BitsF(1800.0));

    // Symbolic binds are present.
    EXPECT_GE(FindFieldBind(m, "HookshotState", "target"), 0);
    EXPECT_GE(FindFieldBind(m, "Transform", "local.position"), 0);
    bool anchorTag = false;
    for (const uint32_t tag : m.TagBinds)
    {
        anchorTag = anchorTag || m.GetString(tag) == "hookshot.anchor";
    }
    EXPECT_TRUE(anchorTag);
    ASSERT_EQ(m.HostEnumFixups.size(), 1u);
    EXPECT_EQ(m.GetString(m.HostEnumFixups[0].MemberName), "Hookshot");
}

TEST(ScriptCompiler, CompiledBehaviorRunsInTheVm)
{
    const ScriptCompileResult result = CompileFixture("pickup.t");
    ASSERT_TRUE(result.Ok) << result.Error.Message;
    const ScriptModule& m = result.Module;

    const int32_t spawn = FindFunction(m, "Pickup.spawn");
    const int32_t fixed = FindFunction(m, "Pickup.fixed");
    ASSERT_GE(spawn, 0);
    ASSERT_GE(fixed, 0);

    const int32_t posY = FindFieldBind(m, "Transform", "local.position.y");
    const int32_t baseHeight = FindFieldBind(m, "BobMotion", "base_height");
    const int32_t amplitude = FindFieldBind(m, "BobMotion", "amplitude");
    const int32_t frequency = FindFieldBind(m, "BobMotion", "frequency");
    ASSERT_GE(posY, 0);
    ASSERT_GE(baseHeight, 0);

    FakeScriptHost host;
    const uint64_t entity = 42;
    host.Components[{entity, static_cast<uint32_t>(posY)}] = {BitsF(100.0)};
    host.Components[{entity, static_cast<uint32_t>(baseHeight)}] = {BitsF(0.0)};
    host.Components[{entity, static_cast<uint32_t>(amplitude)}] = {BitsF(12.0)};
    host.Components[{entity, static_cast<uint32_t>(frequency)}] = {BitsF(0.8)};

    ScriptVm vm;
    ScriptInvokeOptions options;
    const uint64_t args[3] = {entity, BitsL(480), BitsF(1.0 / 60.0)};
    options.Args = args;

    const ScriptInvokeResult spawnResult =
        vm.Invoke(m, static_cast<uint32_t>(spawn), host, options);
    ASSERT_TRUE(spawnResult.Ok())
        << ScriptTrapName(spawnResult.Trap) << " at " << spawnResult.TrapCodeOffset;
    EXPECT_EQ((host.Components[{entity, static_cast<uint32_t>(baseHeight)}][0]), BitsF(100.0));

    const ScriptInvokeResult fixedResult =
        vm.Invoke(m, static_cast<uint32_t>(fixed), host, options);
    ASSERT_TRUE(fixedResult.Ok())
        << ScriptTrapName(fixedResult.Trap) << " at " << fixedResult.TrapCodeOffset;
    // y = base + f32(sin(t * frequency * tau) * amplitude), t = 480/60 = 8.
    const double t = static_cast<double>(static_cast<float>(480.0)) * (1.0 / 60.0);
    (void)t;
    const double y = std::bit_cast<double>(
        host.Components[{entity, static_cast<uint32_t>(posY)}][0]);
    EXPECT_NEAR(y, 100.0, 12.1);
    EXPECT_NE(y, 100.0); // the bob moved it

    // Determinism: run fixed again from the same state, same write.
    host.Components[{entity, static_cast<uint32_t>(posY)}] = {BitsF(100.0)};
    const ScriptInvokeResult again =
        vm.Invoke(m, static_cast<uint32_t>(fixed), host, options);
    ASSERT_TRUE(again.Ok());
    EXPECT_EQ((std::bit_cast<double>(host.Components[{entity, static_cast<uint32_t>(posY)}][0])),
              y);
    EXPECT_EQ(again.InstructionsExecuted, fixedResult.InstructionsExecuted);
}

TEST(ScriptCompiler, StatementTerminationRules)
{
    // Trailing operator continues.
    EXPECT_TRUE(CompileSource("fn f() -> f32 {\n"
                              "    let a = 1.0 +\n"
                              "        2.0\n"
                              "    return a\n"
                              "}\n").Ok);
    // Leading operator continues.
    EXPECT_TRUE(CompileSource("fn f() -> f32 {\n"
                              "    let a = 1.0\n"
                              "        + 2.0\n"
                              "    return a\n"
                              "}\n").Ok);
    // Open paren suppresses terminators.
    EXPECT_TRUE(CompileSource("fn g(x: f32) -> f32 { return x }\n"
                              "fn f() -> f32 {\n"
                              "    return g(\n"
                              "        1.0\n"
                              "    )\n"
                              "}\n").Ok);
    // A newline between complete statements terminates.
    EXPECT_TRUE(CompileSource("fn f() -> i32 {\n"
                              "    let a = 1\n"
                              "    let b = 2\n"
                              "    return a + b\n"
                              "}\n").Ok);
    // A bare non-call expression statement is rejected.
    ExpectError("fn f() {\n"
                "    1 + 2\n"
                "}\n",
                "must be a call", 2);
}

TEST(ScriptCompiler, ErrorMessagesCarryPositions)
{
    ExpectError("fn f() {\n"
                "    let x = unknown_thing\n"
                "}\n",
                "unknown identifier", 2);
    ExpectError("fn f() {\n"
                "    let x = 1 + 2.0\n"
                "}\n",
                "explicit conversion", 2);
    ExpectError("ability A {\n"
                "    fn start(ctx: AbilityContext) {\n"
                "        enter Missing\n"
                "    }\n"
                "}\n",
                "unknown state", 3);
    ExpectError("ability A {\n"
                "    fn wrong_name(ctx: AbilityContext) {\n"
                "    }\n"
                "}\n",
                "unknown callback", 2);
    ExpectError("behavior B {\n"
                "    fn fixed(ctx: AbilityContext) {\n"
                "    }\n"
                "}\n",
                "BehaviorContext", 2);
    ExpectError("fn f() -> i32 {\n"
                "    let a = 1\n"
                "}\n",
                "must end with a return", 1);
    ExpectError("trigger T {\n"
                "    fn on_enter(ctx: TriggerContext) {\n"
                "        enter Something\n"
                "    }\n"
                "}\n",
                "enter is only valid", 3);
}

TEST(ScriptCompiler, ImportRules)
{
    // ".." is rejected.
    const ScriptCompileResult result =
        CompileScript("a.t", "import \"../evil.t\"\n", FixtureResolver());
    EXPECT_FALSE(result.Ok);
    EXPECT_NE(result.Error.Message.find("invalid import path"), std::string::npos);

    // Missing imports are reported.
    const ScriptCompileResult missing =
        CompileScript("a.t", "import \"nope.t\"\n", FixtureResolver());
    EXPECT_FALSE(missing.Ok);
    EXPECT_NE(missing.Error.Message.find("cannot read import"), std::string::npos);
}

TEST(ScriptCompiler, GoldenDisassembly)
{
    for (const std::string_view fixture : {"hookshot.t", "door.t", "pickup.t", "burn_field.t"})
    {
        const ScriptCompileResult result = CompileFixture(fixture);
        ASSERT_TRUE(result.Ok) << fixture << ": " << result.Error.Message;
        const std::string disasm = DisassembleScriptModule(result.Module);

        std::string golden;
        const std::string goldenName = std::string(fixture) + ".disasm";
        ASSERT_TRUE(ReadFixture(goldenName, golden))
            << "missing golden " << goldenName
            << " (generate with the disassembler and check it in)";
        EXPECT_EQ(disasm, golden) << fixture << " disassembly drifted";
    }
}
