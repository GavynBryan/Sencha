// Typed cvar reads with a fallback.
//
// Six places wrote this by hand before it had a home, and the step people skip
// is the type check: a cvar registered as one alternative and read as another
// must yield the fallback, not garbage and not a throw.

#include <gtest/gtest.h>

#include <app/EngineConsoleBuiltins.h>
#include <core/console/CVarRead.h>
#include <core/console/ConsoleRegistry.h>
#include <runtime/RuntimeFrameLoop.h>

namespace
{

struct CVarHarness
{
    ConsoleRegistry Registry;
    RuntimeFrameLoop Loop;
    EngineRuntimeConfig Runtime;

    CVarHarness()
    {
        EngineConsoleBuiltins::RegisterRuntimeCVars(Registry, Loop, Runtime);
        // The runtime builtins register no string tunable; render.debug.view is
        // profiling-gated. One local string cvar keeps the string cases
        // independent of which build flags are on.
        Registry.RegisterCVar({
            .Name = "test.label",
            .Owner = "test",
            .Type = CVarType::String,
            .DefaultValue = std::string("none"),
            .CurrentValue = std::string("none"),
            .Source = { "test" },
        });
    }
};

} // namespace

TEST(CVarRead, FallsBackWithoutARegistry)
{
    EXPECT_DOUBLE_EQ(ReadCVarDouble(nullptr, "render.exposure", 2.0), 2.0);
    EXPECT_FLOAT_EQ(ReadCVarFloat(nullptr, "render.exposure", 2.0f), 2.0f);
    EXPECT_TRUE(ReadCVarBool(nullptr, "render.tonemap", true));
    EXPECT_EQ(ReadCVarString(nullptr, "render.debug.view", "none"), "none");
}

TEST(CVarRead, FallsBackForAnUnregisteredName)
{
    const ConsoleRegistry empty;
    EXPECT_DOUBLE_EQ(ReadCVarDouble(&empty, "nothing.here", 3.5), 3.5);
    EXPECT_FALSE(ReadCVarBool(&empty, "nothing.here", false));
    EXPECT_EQ(ReadCVarString(&empty, "nothing.here", "fallback"), "fallback");
}

TEST(CVarRead, ReadsRegisteredValues)
{
    CVarHarness harness;
    ASSERT_TRUE(harness.Registry.SetCVar("render.exposure", 1.75, { "test" },
                                         ConsolePhase::EngineReady).Succeeded());
    ASSERT_TRUE(harness.Registry.SetCVar("render.tonemap", false, { "test" },
                                         ConsolePhase::EngineReady).Succeeded());

    EXPECT_DOUBLE_EQ(ReadCVarDouble(&harness.Registry, "render.exposure", 0.0), 1.75);
    EXPECT_FLOAT_EQ(ReadCVarFloat(&harness.Registry, "render.exposure", 0.0f), 1.75f);
    EXPECT_FALSE(ReadCVarBool(&harness.Registry, "render.tonemap", true));
}

TEST(CVarRead, FallsBackWhenTheRegisteredTypeIsNotTheRequestedOne)
{
    CVarHarness harness;

    // render.tonemap is a bool. Asking for a double must not reinterpret it,
    // and must not throw the way std::get would.
    EXPECT_DOUBLE_EQ(ReadCVarDouble(&harness.Registry, "render.tonemap", 9.5), 9.5);
    EXPECT_EQ(ReadCVarString(&harness.Registry, "render.tonemap", "fallback"),
              "fallback");

    // render.exposure is a double read as a bool.
    EXPECT_TRUE(ReadCVarBool(&harness.Registry, "render.exposure", true));
    EXPECT_FALSE(ReadCVarBool(&harness.Registry, "render.exposure", false));
}

TEST(CVarRead, FloatIsTheDoubleNarrowed)
{
    CVarHarness harness;
    ASSERT_TRUE(harness.Registry.SetCVar("render.exposure", 0.1, { "test" },
                                         ConsolePhase::EngineReady).Succeeded());

    EXPECT_FLOAT_EQ(ReadCVarFloat(&harness.Registry, "render.exposure", 0.0f),
                    static_cast<float>(0.1));
}

TEST(CVarRead, StringIsReturnedByValue)
{
    CVarHarness harness;
    const std::string first =
        ReadCVarString(&harness.Registry, "test.label", "missing");
    ASSERT_TRUE(harness.Registry.SetCVar("test.label", std::string("changed"),
                                         { "test" },
                                         ConsolePhase::EngineReady).Succeeded());

    // The earlier read must be unaffected by the later write: callers hold the
    // result across frames, and a view into registry storage would not survive.
    EXPECT_EQ(first, "none");
    EXPECT_EQ(ReadCVarString(&harness.Registry, "test.label", "missing"), "changed");
}
