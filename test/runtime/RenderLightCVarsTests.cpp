// One reader for the render.* tunables, shared by the game and the editor.
//
// The editor used to re-read a subset with its own hard-coded fallbacks --
// eight of the fifteen -- so viewports rendered untonemapped, at exposure 1.0,
// with no diffuse wrap or minimum-ambient floor, and ignored both
// baked-lighting toggles. Nothing failed; the viewport just disagreed with the
// game about the settings someone was dialling in while looking at it.
//
// The coverage that matters here is the sweep: every registered tunable, set to
// a distinguishable value, asserted to arrive. A test per field would have been
// written for the eight that already worked.

#include <gtest/gtest.h>

#include <app/EngineConsoleBuiltins.h>
#include <core/console/ConsoleRegistry.h>
#include <render/RenderLight.h>
#include <render/RenderLightCVars.h>
#include <runtime/RuntimeFrameLoop.h>

namespace
{

struct RendererCVarHarness
{
    ConsoleRegistry Registry;
    RuntimeFrameLoop Loop;
    EngineRuntimeConfig Runtime;

    RendererCVarHarness()
    {
        EngineConsoleBuiltins::RegisterRuntimeCVars(Registry, Loop, Runtime);
    }

    void SetDouble(std::string_view name, double value)
    {
        ASSERT_TRUE(Registry.SetCVar(name, value, { "test" },
                                     ConsolePhase::EngineReady).Succeeded())
            << name;
    }

    void SetBool(std::string_view name, bool value)
    {
        ASSERT_TRUE(Registry.SetCVar(name, value, { "test" },
                                     ConsolePhase::EngineReady).Succeeded())
            << name;
    }
};

} // namespace

TEST(ApplyRendererCVars, LeavesEveryFieldAloneWithoutARegistry)
{
    RenderLightSet lights;
    const RenderLightSet defaults;

    ApplyRendererCVars(nullptr, lights);

    EXPECT_EQ(lights.Exposure, defaults.Exposure);
    EXPECT_EQ(lights.TonemapEnabled, defaults.TonemapEnabled);
    EXPECT_EQ(lights.DiffuseWrap, defaults.DiffuseWrap);
    EXPECT_EQ(lights.MinAmbient, defaults.MinAmbient);
    EXPECT_EQ(lights.BakedDirectEnabled, defaults.BakedDirectEnabled);
    EXPECT_EQ(lights.BakedAoEnabled, defaults.BakedAoEnabled);
}

TEST(ApplyRendererCVars, AppliesEveryRegisteredTunable)
{
    RendererCVarHarness harness;

    harness.SetDouble("render.ambient.sky_r", 0.61);
    harness.SetDouble("render.ambient.sky_g", 0.62);
    harness.SetDouble("render.ambient.sky_b", 0.63);
    harness.SetDouble("render.ambient.ground_r", 0.64);
    harness.SetDouble("render.ambient.ground_g", 0.65);
    harness.SetDouble("render.ambient.ground_b", 0.66);
    harness.SetDouble("render.style.diffuse_wrap", 0.67);
    harness.SetDouble("render.style.min_ambient", 0.68);
    harness.SetDouble("render.exposure", 0.69);
    harness.SetDouble("render.tonemap.knee", 0.70);
    harness.SetDouble("render.shadow.darkness", 0.71);
    harness.SetDouble("render.shadow.softness", 0.72);
    harness.SetDouble("render.shadow.bias_const", 0.73);
    harness.SetDouble("render.shadow.bias_slope", 0.74);
    harness.SetBool("render.tonemap", false);
    harness.SetBool("render.baked_direct.enabled", false);
    harness.SetBool("render.ao.enabled", false);

    RenderLightSet lights;
    ApplyRendererCVars(&harness.Registry, lights);

    EXPECT_FLOAT_EQ(lights.AmbientSky.X, 0.61f);
    EXPECT_FLOAT_EQ(lights.AmbientSky.Y, 0.62f);
    EXPECT_FLOAT_EQ(lights.AmbientSky.Z, 0.63f);
    EXPECT_FLOAT_EQ(lights.AmbientGround.X, 0.64f);
    EXPECT_FLOAT_EQ(lights.AmbientGround.Y, 0.65f);
    EXPECT_FLOAT_EQ(lights.AmbientGround.Z, 0.66f);

    // The seven the editor never read.
    EXPECT_FLOAT_EQ(lights.DiffuseWrap, 0.67f);
    EXPECT_FLOAT_EQ(lights.MinAmbient, 0.68f);
    EXPECT_FLOAT_EQ(lights.Exposure, 0.69f);
    EXPECT_FLOAT_EQ(lights.TonemapKnee, 0.70f);
    EXPECT_FALSE(lights.TonemapEnabled);
    EXPECT_FALSE(lights.BakedDirectEnabled);
    EXPECT_FALSE(lights.BakedAoEnabled);

    EXPECT_FLOAT_EQ(lights.ShadowDarkness, 0.71f);
    EXPECT_FLOAT_EQ(lights.ShadowSoftness, 0.72f);
    EXPECT_FLOAT_EQ(lights.ShadowBiasConstant, 0.73f);
    EXPECT_FLOAT_EQ(lights.ShadowBiasSlope, 0.74f);
}

TEST(ApplyRendererCVars, FallsBackToTheLightSetRatherThanARepeatedLiteral)
{
    // An empty registry has none of the tunables. The field must keep whatever
    // the caller had, which is how the defaults stay in one place instead of
    // being restated at each call site.
    ConsoleRegistry empty;
    RenderLightSet lights;
    lights.Exposure = 3.25f;
    lights.MinAmbient = 0.125f;
    lights.TonemapEnabled = false;

    ApplyRendererCVars(&empty, lights);

    EXPECT_FLOAT_EQ(lights.Exposure, 3.25f);
    EXPECT_FLOAT_EQ(lights.MinAmbient, 0.125f);
    EXPECT_FALSE(lights.TonemapEnabled);
}

TEST(ApplyRendererCVars, IsIdempotent)
{
    RendererCVarHarness harness;
    harness.SetDouble("render.exposure", 2.5);

    RenderLightSet once;
    ApplyRendererCVars(&harness.Registry, once);
    RenderLightSet twice;
    ApplyRendererCVars(&harness.Registry, twice);
    ApplyRendererCVars(&harness.Registry, twice);

    EXPECT_FLOAT_EQ(once.Exposure, twice.Exposure);
    EXPECT_FLOAT_EQ(twice.Exposure, 2.5f);
}
