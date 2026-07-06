#include "ScriptTestSupport.h"

#include <core/hash/ContentHash.h>
#include <ecs/World.h>
#include <script/ScriptBehaviorSystem.h>
#include <script/ScriptCompiler.h>
#include <script/ScriptLink.h>
#include <script/ScriptRuntime.h>
#include <world/transform/TransformComponents.h>

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>

namespace
{
    struct BobMotionBytes
    {
        float BaseHeight;
        float Amplitude;
        float Frequency;
    };

    std::shared_ptr<const ScriptModule> CompilePickup()
    {
        std::ifstream file(SCRIPT_FIXTURE_DIR "/pickup.t");
        EXPECT_TRUE(file.good());
        std::ostringstream text;
        text << file.rdbuf();
        const std::string source = text.str();
        ScriptCompileResult result =
            CompileScript("pickup.t", source, [](std::string_view path, std::string& out) {
                std::ifstream in(std::string(SCRIPT_FIXTURE_DIR "/") + std::string(path));
                if (!in)
                {
                    return false;
                }
                std::ostringstream t;
                t << in.rdbuf();
                out = t.str();
                return true;
            });
        EXPECT_TRUE(result.Ok) << result.Error.Message;
        return std::make_shared<const ScriptModule>(std::move(result.Module));
    }

    // Reference implementation of the pickup formula, mirroring the script and
    // the engine's deterministic kernels via the same f32 rounding rules.
    float ReferenceBob(uint64_t tick, float dt, float base, float freq, float amp)
    {
        const float t = static_cast<float>(static_cast<double>(tick) * static_cast<double>(dt));
        // bob_offset returns f32(sin(t * freq * tau) * amp)
        constexpr double tau = 6.283185307179586476925286766559;
        const double inner = static_cast<double>(t) * static_cast<double>(freq) * tau;
        // The script's sin is the deterministic kernel, but for a tolerance
        // check std::sin is close enough.
        const double offset = std::sin(inner) * static_cast<double>(amp);
        return base + static_cast<float>(offset);
    }
}

TEST(ScriptBehaviorE2E, PickupBobsDeterministically)
{
    std::shared_ptr<const ScriptModule> module = CompilePickup();

    World world;
    // Registration happens before any entity exists (host component, script
    // runtime, then the module's script components through linking).
    world.RegisterComponent<LocalTransform>();
    RegisterScriptRuntime(world, /*worldSeed*/ 1234);

    const ScriptLinkResult link = LinkScriptModule(world, module);
    ASSERT_TRUE(link.Ok) << link.Error;

    const ComponentId bobId =
        world.GetComponentIdByType(MakeComponentTypeId("script.BobMotion"));
    ASSERT_NE(bobId, InvalidComponentId);

    // Spawn a pickup entity at y = 100.
    const EntityId entity = world.CreateEntity();
    LocalTransform xform{};
    xform.Value.Position.X = 0.0f;
    xform.Value.Position.Y = 100.0f;
    xform.Value.Position.Z = 0.0f;
    world.AddComponent(entity, xform);

    BobMotionBytes bob{ /*base*/ 0.0f, /*amp*/ 12.0f, /*freq*/ 0.8f };
    world.AddComponentRaw(entity, bobId, &bob, sizeof(bob), alignof(BobMotionBytes), nullptr);

    ScriptBehavior behavior{};
    behavior.LinkedModule = link.ModuleIndex;
    behavior.Declaration = 0; // the Pickup behavior
    world.AddComponent(entity, behavior);

    const float dt = 1.0f / 60.0f;
    ScriptBehaviorSystem system;

    // Tick 0 runs spawn (captures base_height = 100) then fixed.
    system.Step(world, 0, dt);
    {
        const BobMotionBytes* stored =
            static_cast<const BobMotionBytes*>(world.GetComponentRaw(entity, bobId));
        EXPECT_FLOAT_EQ(stored->BaseHeight, 100.0f);
    }

    for (uint64_t tick = 1; tick <= 8; ++tick)
    {
        system.Step(world, tick, dt);
        const LocalTransform* xf = world.TryGet<LocalTransform>(entity);
        ASSERT_NE(xf, nullptr);
        const float expected = ReferenceBob(tick, dt, 100.0f, 0.8f, 12.0f);
        EXPECT_NEAR(xf->Value.Position.Y, expected, 1e-3f) << "tick " << tick;
    }
}

TEST(ScriptBehaviorE2E, RunToRunReproducible)
{
    // Two independent worlds driven identically must produce identical
    // per-tick position streams (the determinism gate, single-process form).
    auto build = [](std::vector<float>& trajectory) {
        std::shared_ptr<const ScriptModule> module = CompilePickup();
        World world;
        world.RegisterComponent<LocalTransform>();
        RegisterScriptRuntime(world, 1234);
        const ScriptLinkResult link = LinkScriptModule(world, module);
        ASSERT_TRUE(link.Ok) << link.Error;
        const ComponentId bobId =
            world.GetComponentIdByType(MakeComponentTypeId("script.BobMotion"));

        const EntityId e = world.CreateEntity();
        LocalTransform xform{};
        xform.Value.Position.X = 0.0f;
        xform.Value.Position.Y = 50.0f;
        xform.Value.Position.Z = 0.0f;
        world.AddComponent(e, xform);
        BobMotionBytes bob{ 0.0f, 7.5f, 1.3f };
        world.AddComponentRaw(e, bobId, &bob, sizeof(bob), alignof(BobMotionBytes), nullptr);
        ScriptBehavior behavior{};
        behavior.LinkedModule = link.ModuleIndex;
        world.AddComponent(e, behavior);

        ScriptBehaviorSystem system;
        for (uint64_t tick = 0; tick <= 20; ++tick)
        {
            system.Step(world, tick, 1.0f / 60.0f);
            trajectory.push_back(world.TryGet<LocalTransform>(e)->Value.Position.Y);
        }
    };

    std::vector<float> a;
    std::vector<float> b;
    build(a);
    build(b);
    ASSERT_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i)
    {
        EXPECT_EQ(std::bit_cast<uint32_t>(a[i]), std::bit_cast<uint32_t>(b[i]))
            << "divergence at tick " << i;
    }
}

TEST(ScriptBehaviorE2E, CommandsAddMarshalsRecordImage)
{
    // A behavior adds a script component to itself on spawn; the deferred
    // command applies at the tick boundary and the next tick sees the values.
    ScriptCompileResult compiled = CompileScript(
        "adder.t",
        "component Marker {\n"
        "    count: i32 = 0\n"
        "    weight: f32 = 0.0\n"
        "}\n"
        "behavior Adder {\n"
        "    fn spawn(ctx: BehaviorContext) {\n"
        "        ctx.commands.add(ctx.entity, Marker { count: 7, weight: 2.5 })\n"
        "    }\n"
        "}\n",
        {});
    ASSERT_TRUE(compiled.Ok) << compiled.Error.Message;

    World world;
    RegisterScriptRuntime(world);
    const ScriptLinkResult link =
        LinkScriptModule(world, std::make_shared<const ScriptModule>(std::move(compiled.Module)));
    ASSERT_TRUE(link.Ok) << link.Error;

    const ComponentId markerId =
        world.GetComponentIdByType(MakeComponentTypeId("script.Marker"));
    ASSERT_NE(markerId, InvalidComponentId);

    const EntityId entity = world.CreateEntity();
    ScriptBehavior behavior{};
    behavior.LinkedModule = link.ModuleIndex;
    world.AddComponent(entity, behavior);

    ScriptBehaviorSystem system;
    system.Step(world, 0, 1.0f / 60.0f); // spawn enqueues the add; flush applies it
    ASSERT_TRUE(world.HasComponent(entity, markerId));

    struct MarkerBytes { int32_t Count; float Weight; };
    const MarkerBytes* stored =
        static_cast<const MarkerBytes*>(world.GetComponentRaw(entity, markerId));
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(stored->Count, 7);
    EXPECT_FLOAT_EQ(stored->Weight, 2.5f);
}

TEST(ScriptBehaviorE2E, CueFireAppendsToBuffer)
{
    // A behavior fires a cue on spawn; it lands in the entity's cue buffer as
    // the stable content hash of the cue path.
    ScriptCompileResult compiled = CompileScript(
        "cue.t",
        "behavior Chime {\n"
        "    fn spawn(ctx: BehaviorContext) {\n"
        "        ctx.cue(cue\"ui.chime\")\n"
        "    }\n"
        "}\n",
        {});
    ASSERT_TRUE(compiled.Ok) << compiled.Error.Message;

    World world;
    RegisterScriptRuntime(world);
    const ScriptLinkResult link =
        LinkScriptModule(world, std::make_shared<const ScriptModule>(std::move(compiled.Module)));
    ASSERT_TRUE(link.Ok) << link.Error;

    const EntityId entity = world.CreateEntity();
    world.AddComponent(entity, ScriptCueBuffer{});
    ScriptBehavior behavior{};
    behavior.LinkedModule = link.ModuleIndex;
    world.AddComponent(entity, behavior);

    ScriptBehaviorSystem system;
    system.Step(world, 0, 1.0f / 60.0f);

    const ScriptCueBuffer* cues = world.TryGet<ScriptCueBuffer>(entity);
    ASSERT_NE(cues, nullptr);
    ASSERT_EQ(cues->Count, 1);
    EXPECT_EQ(cues->Events[0].CueHash, HashBytes64(std::string_view("ui.chime")));
    EXPECT_EQ(cues->Events[0].HasPosition, 0);
}

TEST(ScriptLink, ReportsMissingComponent)
{
    // A script naming a host component the world has not registered fails to
    // link with a clear message.
    ScriptCompileResult compiled = CompileScript(
        "t.t",
        "behavior B {\n"
        "    fn fixed(ctx: BehaviorContext) {\n"
        "        ctx.entity.Transform.position.y = 1.0\n"
        "    }\n"
        "}\n",
        {});
    ASSERT_TRUE(compiled.Ok) << compiled.Error.Message;

    World world;
    RegisterScriptRuntime(world);
    // LocalTransform deliberately not registered.
    const ScriptLinkResult link =
        LinkScriptModule(world, std::make_shared<const ScriptModule>(std::move(compiled.Module)));
    EXPECT_FALSE(link.Ok);
    EXPECT_NE(link.Error.find("Transform"), std::string::npos) << link.Error;
}
