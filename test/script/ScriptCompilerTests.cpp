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

TEST(ScriptCompiler, CompiledSystemRunsInTheVm)
{
    const ScriptCompileResult result = CompileFixture("pickup.t");
    ASSERT_TRUE(result.Ok) << result.Error.Message;
    const ScriptModule& m = result.Module;

    const int32_t fixed = FindFunction(m, "Bob.fixed");
    ASSERT_GE(fixed, 0);

    const int32_t posY = FindFieldBind(m, "Transform", "local.position.y");
    const int32_t baseHeight = FindFieldBind(m, "BobMotion", "base_height");
    const int32_t amplitude = FindFieldBind(m, "BobMotion", "amplitude");
    const int32_t frequency = FindFieldBind(m, "BobMotion", "frequency");
    ASSERT_GE(posY, 0);
    ASSERT_GE(baseHeight, 0);

    FakeScriptHost host;
    const uint64_t entity = 42;
    // base_height is authored (no spawn capture in the system form).
    host.Components[{entity, static_cast<uint32_t>(posY)}] = {BitsF(100.0)};
    host.Components[{entity, static_cast<uint32_t>(baseHeight)}] = {BitsF(100.0)};
    host.Components[{entity, static_cast<uint32_t>(amplitude)}] = {BitsF(12.0)};
    host.Components[{entity, static_cast<uint32_t>(frequency)}] = {BitsF(0.8)};

    ScriptVm vm;
    ScriptInvokeOptions options;
    const uint64_t args[3] = {entity, BitsL(480), BitsF(1.0 / 60.0)};
    options.Args = args;

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
    EXPECT_TRUE(CompileSource("fn f(): f32 {\n"
                              "    let a = 1.0 +\n"
                              "        2.0\n"
                              "    return a\n"
                              "}\n").Ok);
    // Leading operator continues.
    EXPECT_TRUE(CompileSource("fn f(): f32 {\n"
                              "    let a = 1.0\n"
                              "        + 2.0\n"
                              "    return a\n"
                              "}\n").Ok);
    // Open paren suppresses terminators.
    EXPECT_TRUE(CompileSource("fn g(x: f32): f32 { return x }\n"
                              "fn f(): f32 {\n"
                              "    return g(\n"
                              "        1.0\n"
                              "    )\n"
                              "}\n").Ok);
    // A newline between complete statements terminates.
    EXPECT_TRUE(CompileSource("fn f(): i32 {\n"
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
    ExpectError("fn f(): i32 {\n"
                "    let a = 1\n"
                "}\n",
                "must end with a return", 1);
    ExpectError("trigger T {\n"
                "    fn on_enter(ctx: TriggerContext) {\n"
                "        enter Something\n"
                "    }\n"
                "}\n",
                "enter is only valid", 3);
    // Return types use ':'; the arrow is rejected with guidance.
    ExpectError("fn f() -> i32 {\n"
                "    return 1\n"
                "}\n",
                "use ':' for return types", 1);
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

TEST(ScriptCompiler, RequiresClauseLowersAndRoundTrips)
{
    const ScriptCompileResult r = CompileSource(
        "component Health { current: i32 = 0 }\n"
        "component Mana { current: i32 = 0 }\n"
        "behavior Regen {\n"
        "    requires Health, Mana\n"
        "    fn fixed(ctx: BehaviorContext) {\n"
        "        ctx.entity.Health.current += 1\n"
        "    }\n"
        "}\n");
    ASSERT_TRUE(r.Ok) << r.Error.Message;

    auto findRegen = [](const ScriptModule& m) -> const ScriptDeclaration* {
        for (const ScriptDeclaration& d : m.Declarations)
            if (m.GetString(d.Name) == "Regen")
                return &d;
        return nullptr;
    };

    const ScriptDeclaration* decl = findRegen(r.Module);
    ASSERT_NE(decl, nullptr);
    ASSERT_EQ(decl->RequiredComponents.size(), 2u);
    EXPECT_EQ(r.Module.GetString(decl->RequiredComponents[0]), "Health");
    EXPECT_EQ(r.Module.GetString(decl->RequiredComponents[1]), "Mana");

    // The required set survives the container round-trip (format v1).
    const std::vector<std::byte> bytes = WriteScriptModule(r.Module);
    const ScriptModuleParseResult parsed = ParseScriptModule(bytes);
    ASSERT_TRUE(parsed.Ok) << parsed.Error;
    const ScriptDeclaration* rt = findRegen(parsed.Module);
    ASSERT_NE(rt, nullptr);
    ASSERT_EQ(rt->RequiredComponents.size(), 2u);
    EXPECT_EQ(parsed.Module.GetString(rt->RequiredComponents[0]), "Health");
    EXPECT_EQ(parsed.Module.GetString(rt->RequiredComponents[1]), "Mana");
}

TEST(ScriptNumeric, StoringF64IntoF32FieldRequiresCast)
{
    // sin() is f64 -> f64; the field is f32. The narrowing must be an explicit cast.
    ExpectError(
        "component Motion {\n"
        "    y: f32 = 0.0\n"
        "}\n"
        "behavior Bob {\n"
        "    fn fixed(ctx: BehaviorContext) {\n"
        "        ctx.entity.Motion.y = sin(ctx.dt) * 10.0\n"
        "    }\n"
        "}\n",
        "f32 field");
}

TEST(ScriptNumeric, F32CastBuildsTheNarrowingStore)
{
    const ScriptCompileResult r = CompileSource(
        "component Motion {\n"
        "    y: f32 = 0.0\n"
        "}\n"
        "behavior Bob {\n"
        "    fn fixed(ctx: BehaviorContext) {\n"
        "        ctx.entity.Motion.y = f32(sin(ctx.dt) * 10.0)\n"
        "    }\n"
        "}\n");
    EXPECT_TRUE(r.Ok) << r.Error.Message;
}

TEST(ScriptNumeric, NoOpAndRedundantCastsFail)
{
    // f32(x) where x is already f32 narrows nothing.
    ExpectError(
        "behavior Bob {\n"
        "    fn fixed(ctx: BehaviorContext) {\n"
        "        let a: f32 = f32(ctx.dt)\n"
        "    }\n"
        "}\n",
        "narrows nothing");

    // f64(x) where x is f32 is a redundant widening (f32 -> f64 is implicit).
    ExpectError(
        "behavior Bob {\n"
        "    fn fixed(ctx: BehaviorContext) {\n"
        "        let a: f64 = f64(ctx.dt)\n"
        "    }\n"
        "}\n",
        "redundant");
}

TEST(ScriptCompilerSystem, CompilesSystemWithOverAndFixed)
{
    const ScriptCompileResult r = CompileSource(
        "component Health { current: i32 = 0 }\n"
        "system Regen {\n"
        "    over Health\n"
        "    writes Health\n"
        "    fn fixed(ctx: FixedContext) {\n"
        "        ctx.entity.Health.current += 1\n"
        "    }\n"
        "}\n");
    ASSERT_TRUE(r.Ok) << r.Error.Message;

    ASSERT_EQ(r.Module.Declarations.size(), 1u);
    const ScriptDeclaration& d = r.Module.Declarations[0];
    EXPECT_EQ(r.Module.GetString(d.Name), "Regen");
    EXPECT_EQ(d.Kind, ScriptDeclKind::System);
    ASSERT_EQ(d.RequiredComponents.size(), 1u);
    EXPECT_EQ(r.Module.GetString(d.RequiredComponents[0]), "Health");
    ASSERT_EQ(d.Callbacks.size(), 1u);
    EXPECT_EQ(r.Module.GetString(d.Callbacks[0].Name), "fixed");

    EXPECT_TRUE(ValidateScriptModule(r.Module).Ok);

    // The System declaration kind (5) round-trips through the .tbc container.
    const std::vector<std::byte> bytes = WriteScriptModule(r.Module);
    const ScriptModuleParseResult parsed = ParseScriptModule(bytes);
    ASSERT_TRUE(parsed.Ok) << parsed.Error;
    ASSERT_EQ(parsed.Module.Declarations.size(), 1u);
    EXPECT_EQ(parsed.Module.Declarations[0].Kind, ScriptDeclKind::System);
}

TEST(ScriptCompilerSystem, CompilesWithoutClauseAndRoundTrips)
{
    const ScriptCompileResult r = CompileSource(
        "component A { x: i32 = 0 }\n"
        "component B { y: i32 = 0 }\n"
        "system S {\n"
        "    over A\n"
        "    without B\n"
        "    writes A\n"
        "    fn fixed(ctx: FixedContext) { ctx.entity.A.x += 1 }\n"
        "}\n");
    ASSERT_TRUE(r.Ok) << r.Error.Message;
    ASSERT_EQ(r.Module.Declarations.size(), 1u);
    const ScriptDeclaration& d = r.Module.Declarations[0];
    ASSERT_EQ(d.ExcludedComponents.size(), 1u);
    EXPECT_EQ(r.Module.GetString(d.ExcludedComponents[0]), "B");

    const std::vector<std::byte> bytes = WriteScriptModule(r.Module);
    const ScriptModuleParseResult parsed = ParseScriptModule(bytes);
    ASSERT_TRUE(parsed.Ok) << parsed.Error;
    ASSERT_EQ(parsed.Module.Declarations.size(), 1u);
    ASSERT_EQ(parsed.Module.Declarations[0].ExcludedComponents.size(), 1u);
    EXPECT_EQ(parsed.Module.GetString(parsed.Module.Declarations[0].ExcludedComponents[0]), "B");
}

TEST(ScriptCompilerSystem, RejectsNonFixedCallback)
{
    ExpectError(
        "component Health { current: i32 = 0 }\n"
        "system Regen {\n"
        "    over Health\n"
        "    fn spawn(ctx: FixedContext) {}\n"
        "}\n",
        "unknown callback");
}

TEST(ScriptCompilerSystem, RejectsWrongContextType)
{
    ExpectError(
        "component Health { current: i32 = 0 }\n"
        "system Regen {\n"
        "    over Health\n"
        "    fn fixed(ctx: BehaviorContext) {}\n"
        "}\n",
        "FixedContext");
}

TEST(ScriptCompilerSystem, RejectsWriteOfUndeclaredComponent)
{
    ExpectError(
        "component Health { current: i32 = 0 }\n"
        "system Regen {\n"
        "    over Health\n"
        "    fn fixed(ctx: FixedContext) {\n"
        "        ctx.entity.Health.current += 1\n"
        "    }\n"
        "}\n",
        "does not declare it in `writes`");
}

TEST(ScriptCompilerSystem, RejectsWritesNotInOver)
{
    ExpectError(
        "component Health { current: i32 = 0 }\n"
        "component Mana { current: i32 = 0 }\n"
        "system Regen {\n"
        "    over Health\n"
        "    writes Mana\n"
        "    fn fixed(ctx: FixedContext) {}\n"
        "}\n",
        "not in its `over` set");
}

TEST(ScriptCompilerSystem, RejectsScheduleCycle)
{
    ExpectError(
        "component Health { current: i32 = 0 }\n"
        "system A {\n"
        "    over Health\n"
        "    after B\n"
        "    fn fixed(ctx: FixedContext) {}\n"
        "}\n"
        "system B {\n"
        "    over Health\n"
        "    after A\n"
        "    fn fixed(ctx: FixedContext) {}\n"
        "}\n",
        "cycle");
}

TEST(ScriptCompilerSystem, RejectsUnknownSystemInAfter)
{
    ExpectError(
        "component Health { current: i32 = 0 }\n"
        "system A {\n"
        "    over Health\n"
        "    after Nope\n"
        "    fn fixed(ctx: FixedContext) {}\n"
        "}\n",
        "unknown system 'Nope'");
}

TEST(ScriptCompilerSystem, RejectsUnorderedSameComponentWriters)
{
    ExpectError(
        "component Health { current: i32 = 0 }\n"
        "system A {\n"
        "    over Health\n"
        "    writes Health\n"
        "    fn fixed(ctx: FixedContext) { ctx.entity.Health.current = 1 }\n"
        "}\n"
        "system B {\n"
        "    over Health\n"
        "    writes Health\n"
        "    fn fixed(ctx: FixedContext) { ctx.entity.Health.current = 2 }\n"
        "}\n",
        "both write 'Health'");
}

TEST(ScriptCompilerSystem, OrderedSameComponentWritersOk)
{
    const ScriptCompileResult r = CompileSource(
        "component Health { current: i32 = 0 }\n"
        "system A {\n"
        "    over Health\n"
        "    writes Health\n"
        "    fn fixed(ctx: FixedContext) { ctx.entity.Health.current = 1 }\n"
        "}\n"
        "system B {\n"
        "    over Health\n"
        "    writes Health\n"
        "    after A\n"
        "    fn fixed(ctx: FixedContext) { ctx.entity.Health.current = 2 }\n"
        "}\n");
    ASSERT_TRUE(r.Ok) << r.Error.Message;
}

TEST(ScriptCompilerSystem, ConfigComponentIsReadOnly)
{
    ExpectError(
        "#[config] component Cfg { speed: f32 = 1.0 }\n"
        "system Move {\n"
        "    over Cfg\n"
        "    fn fixed(ctx: FixedContext) { ctx.entity.Cfg.speed = 2.0 }\n"
        "}\n",
        "read-only");
}

TEST(ScriptCompilerSystem, ConfigComponentIsReadable)
{
    const ScriptCompileResult r = CompileSource(
        "#[config] component Cfg { speed: f32 = 1.0 }\n"
        "component Vel { v: f32 = 0.0 }\n"
        "system Move {\n"
        "    over Cfg, Vel\n"
        "    writes Vel\n"
        "    fn fixed(ctx: FixedContext) { ctx.entity.Vel.v = ctx.entity.Cfg.speed }\n"
        "}\n");
    ASSERT_TRUE(r.Ok) << r.Error.Message;
}

TEST(ScriptCompilerSystem, RuntimeComponentIsWritable)
{
    const ScriptCompileResult r = CompileSource(
        "#[runtime] component St { n: i32 = 0 }\n"
        "system Tick {\n"
        "    over St\n"
        "    writes St\n"
        "    fn fixed(ctx: FixedContext) { ctx.entity.St.n += 1 }\n"
        "}\n");
    ASSERT_TRUE(r.Ok) << r.Error.Message;
}

TEST(ScriptCompilerSystem, AllowsReadingOverComponentNotInWrites)
{
    // Reads of over-components are unrestricted; only writes need declaring.
    const ScriptCompileResult r = CompileSource(
        "component Health { current: i32 = 0 }\n"
        "component Log { n: i32 = 0 }\n"
        "system Watch {\n"
        "    over Health, Log\n"
        "    writes Log\n"
        "    fn fixed(ctx: FixedContext) {\n"
        "        ctx.entity.Log.n = ctx.entity.Health.current\n"
        "    }\n"
        "}\n");
    ASSERT_TRUE(r.Ok) << r.Error.Message;
}

TEST(ScriptCompilerObserver, CompilesOnAddObserver)
{
    const ScriptCompileResult r = CompileSource(
        "component Trigger { x: i32 = 0 }\n"
        "component Marker { n: i32 = 0 }\n"
        "observer Watch {\n"
        "    over Trigger\n"
        "    fn on_add(ctx: ObserveCtx) {\n"
        "        ctx.commands.add(ctx.entity, Marker { n: 42 })\n"
        "    }\n"
        "}\n");
    ASSERT_TRUE(r.Ok) << r.Error.Message;
    ASSERT_EQ(r.Module.Declarations.size(), 1u);
    EXPECT_EQ(r.Module.Declarations[0].Kind, ScriptDeclKind::Observer);
    ASSERT_EQ(r.Module.Declarations[0].RequiredComponents.size(), 1u);
    EXPECT_EQ(r.Module.GetString(r.Module.Declarations[0].RequiredComponents[0]), "Trigger");
    ASSERT_EQ(r.Module.Declarations[0].Callbacks.size(), 1u);
    EXPECT_EQ(r.Module.GetString(r.Module.Declarations[0].Callbacks[0].Name), "on_add");
    EXPECT_TRUE(ValidateScriptModule(r.Module).Ok);
}

TEST(ScriptCompilerObserver, RejectsImmediateComponentWrite)
{
    ExpectError(
        "component Trigger { x: i32 = 0 }\n"
        "observer Watch {\n"
        "    over Trigger\n"
        "    fn on_add(ctx: ObserveCtx) { ctx.entity.Trigger.x = 1 }\n"
        "}\n",
        "cannot write component fields immediately");
}

TEST(ScriptCompilerObserver, RejectsWrongContextType)
{
    ExpectError(
        "component Trigger { x: i32 = 0 }\n"
        "observer Watch {\n"
        "    over Trigger\n"
        "    fn on_add(ctx: FixedContext) {}\n"
        "}\n",
        "ObserveCtx");
}

TEST(ScriptCompilerBehavior, DesugarsConfigRuntimeToComponentsAndSystems)
{
    const ScriptCompileResult r = CompileSource(
        "behavior Counter {\n"
        "    config { step: i32 = 2 }\n"
        "    runtime { n: i32 = 0 }\n"
        "    fn fixed(ctx: FixedContext) {\n"
        "        runtime.n += config.step\n"
        "    }\n"
        "}\n");
    ASSERT_TRUE(r.Ok) << r.Error.Message;

    auto hasComponent = [&](std::string_view name) {
        for (const ScriptComponentDef& c : r.Module.Components)
            if (r.Module.GetString(c.Name) == name) return true;
        return false;
    };
    EXPECT_TRUE(hasComponent("CounterConfig"));
    EXPECT_TRUE(hasComponent("CounterState"));

    auto findDecl = [&](std::string_view name) -> const ScriptDeclaration* {
        for (const ScriptDeclaration& d : r.Module.Declarations)
            if (r.Module.GetString(d.Name) == name) return &d;
        return nullptr;
    };
    const ScriptDeclaration* activate = findDecl("CounterActivate");
    const ScriptDeclaration* fixed = findDecl("CounterFixed");
    ASSERT_NE(activate, nullptr);
    ASSERT_NE(fixed, nullptr);
    EXPECT_EQ(activate->Kind, ScriptDeclKind::System);
    EXPECT_EQ(fixed->Kind, ScriptDeclKind::System);
    // Activate: over CounterConfig, without CounterState.
    ASSERT_EQ(activate->RequiredComponents.size(), 1u);
    EXPECT_EQ(r.Module.GetString(activate->RequiredComponents[0]), "CounterConfig");
    ASSERT_EQ(activate->ExcludedComponents.size(), 1u);
    EXPECT_EQ(r.Module.GetString(activate->ExcludedComponents[0]), "CounterState");
    // Fixed: over CounterConfig + CounterState.
    EXPECT_EQ(fixed->RequiredComponents.size(), 2u);
    // The behavior block itself did not survive as a declaration.
    EXPECT_EQ(findDecl("Counter"), nullptr);

    EXPECT_TRUE(ValidateScriptModule(r.Module).Ok);
}

TEST(ScriptCompilerBehavior, LegacyBehaviorWithoutConfigRuntimeIsUnchanged)
{
    // A behavior with no config/runtime blocks keeps the existing lowering.
    const ScriptCompileResult r = CompileSource(
        "component Health { current: i32 = 0 }\n"
        "behavior Regen {\n"
        "    requires Health\n"
        "    fn fixed(ctx: BehaviorContext) {\n"
        "        ctx.entity.Health.current += 1\n"
        "    }\n"
        "}\n");
    ASSERT_TRUE(r.Ok) << r.Error.Message;
    bool hasBehaviorDecl = false;
    for (const ScriptDeclaration& d : r.Module.Declarations)
        if (d.Kind == ScriptDeclKind::Behavior) hasBehaviorDecl = true;
    EXPECT_TRUE(hasBehaviorDecl);
}

TEST(ScriptCompilerBehavior, DesugarsDespawnAndDetachToObservers)
{
    const ScriptCompileResult r = CompileSource(
        "component Gravestone {}\n"
        "behavior Counter {\n"
        "    config { step: i32 = 2 }\n"
        "    runtime { n: i32 = 0 }\n"
        "    fn fixed(ctx: FixedContext) { runtime.n += config.step }\n"
        "    fn despawn(ctx: ObserveCtx) { ctx.commands.add(ctx.entity, Gravestone {}) }\n"
        "}\n");
    ASSERT_TRUE(r.Ok) << r.Error.Message;
    auto findDecl = [&](std::string_view name) -> const ScriptDeclaration* {
        for (const ScriptDeclaration& d : r.Module.Declarations)
            if (r.Module.GetString(d.Name) == name) return &d;
        return nullptr;
    };
    const ScriptDeclaration* despawn = findDecl("CounterDespawn");
    const ScriptDeclaration* detach = findDecl("CounterDetach");
    ASSERT_NE(despawn, nullptr);
    ASSERT_NE(detach, nullptr);
    EXPECT_EQ(despawn->Kind, ScriptDeclKind::Observer);
    EXPECT_EQ(detach->Kind, ScriptDeclKind::Observer);
    ASSERT_EQ(despawn->RequiredComponents.size(), 1u);
    ASSERT_EQ(detach->RequiredComponents.size(), 1u);
    EXPECT_EQ(r.Module.GetString(despawn->RequiredComponents[0]), "CounterState");
    EXPECT_EQ(r.Module.GetString(detach->RequiredComponents[0]), "CounterConfig");
}

TEST(ScriptCompilerBehavior, SpawnStraightLineInitializersCompile)
{
    // Straight-line `runtime.<field> = <expr>` lines lower into the record that
    // BActivate adds; config reads are allowed in the initializer expressions.
    const ScriptCompileResult r = CompileSource(
        "behavior Counter {\n"
        "    config { step: i32 = 2 }\n"
        "    runtime {\n"
        "        n: i32 = 0\n"
        "        m: i32 = 0\n"
        "    }\n"
        "    fn spawn(ctx: FixedContext) {\n"
        "        runtime.n = config.step\n"
        "        runtime.m = 7\n"
        "    }\n"
        "    fn fixed(ctx: FixedContext) { runtime.n += config.step }\n"
        "}\n");
    ASSERT_TRUE(r.Ok) << r.Error.Message;
    EXPECT_TRUE(ValidateScriptModule(r.Module).Ok);
}

TEST(ScriptCompilerBehavior, RejectsSpawnControlFlow)
{
    // spawn is straight-line field initializers only; per-entity logic belongs
    // in fixed. A control-flow statement is a compile error, not a silent drop.
    ExpectError(
        "behavior Counter {\n"
        "    config { step: i32 = 2 }\n"
        "    runtime { n: i32 = 0 }\n"
        "    fn spawn(ctx: FixedContext) {\n"
        "        if config.step > 0 { runtime.n = 5 }\n"
        "    }\n"
        "}\n",
        "runtime.<field> = <expr>");
}

TEST(ScriptCompilerBehavior, RejectsSpawnReadingRuntime)
{
    // The initializer runs before State exists, so reading runtime is invalid.
    ExpectError(
        "behavior Counter {\n"
        "    config { step: i32 = 2 }\n"
        "    runtime {\n"
        "        n: i32 = 0\n"
        "        m: i32 = 0\n"
        "    }\n"
        "    fn spawn(ctx: FixedContext) {\n"
        "        runtime.n = 5\n"
        "        runtime.m = runtime.n\n"
        "    }\n"
        "}\n",
        "runtime");
}
