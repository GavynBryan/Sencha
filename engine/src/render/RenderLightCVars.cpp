#include <render/RenderLightCVars.h>

#include <core/console/CVarRead.h>
#include <render/RenderLight.h>

void ApplyRendererCVars(const ConsoleRegistry* console, RenderLightSet& lights)
{
    lights.AmbientSky = Vec<3>(
        ReadCVarFloat(console, "render.ambient.sky_r", lights.AmbientSky.X),
        ReadCVarFloat(console, "render.ambient.sky_g", lights.AmbientSky.Y),
        ReadCVarFloat(console, "render.ambient.sky_b", lights.AmbientSky.Z));
    lights.AmbientGround = Vec<3>(
        ReadCVarFloat(console, "render.ambient.ground_r", lights.AmbientGround.X),
        ReadCVarFloat(console, "render.ambient.ground_g", lights.AmbientGround.Y),
        ReadCVarFloat(console, "render.ambient.ground_b", lights.AmbientGround.Z));
    lights.DiffuseWrap = ReadCVarFloat(
        console, "render.style.diffuse_wrap", lights.DiffuseWrap);
    lights.MinAmbient = ReadCVarFloat(
        console, "render.style.min_ambient", lights.MinAmbient);
    lights.Exposure = ReadCVarFloat(console, "render.exposure", lights.Exposure);
    lights.TonemapKnee = ReadCVarFloat(
        console, "render.tonemap.knee", lights.TonemapKnee);
    lights.TonemapEnabled = ReadCVarBool(
        console, "render.tonemap", lights.TonemapEnabled);
    lights.ShadowDarkness = ReadCVarFloat(
        console, "render.shadow.darkness", lights.ShadowDarkness);
    lights.ShadowSoftness = ReadCVarFloat(
        console, "render.shadow.softness", lights.ShadowSoftness);
    lights.ShadowBiasConstant = ReadCVarFloat(
        console, "render.shadow.bias_const", lights.ShadowBiasConstant);
    lights.ShadowBiasSlope = ReadCVarFloat(
        console, "render.shadow.bias_slope", lights.ShadowBiasSlope);
    lights.BakedDirectEnabled = ReadCVarBool(
        console, "render.baked_direct.enabled", lights.BakedDirectEnabled);
    lights.BakedAoEnabled = ReadCVarBool(
        console, "render.ao.enabled", lights.BakedAoEnabled);
    lights.SkyEnabled = ReadCVarBool(console, "render.sky.enabled", lights.SkyEnabled);
    lights.ProjectedShadowsEnabled =
        ReadCVarBool(console, "render.shadow.projected", lights.ProjectedShadowsEnabled);
    lights.ProjectedShadowDarkness = ReadCVarFloat(
        console, "render.shadow.projected.darkness", lights.ProjectedShadowDarkness);
    lights.ProjectedShadowMaxDistance = ReadCVarFloat(
        console, "render.shadow.projected.max_distance", lights.ProjectedShadowMaxDistance);
    lights.ProjectedShadowFadeStart = ReadCVarFloat(
        console, "render.shadow.projected.fade_start", lights.ProjectedShadowFadeStart);
    lights.ProjectedShadowSmoothing = ReadCVarFloat(
        console, "render.shadow.projected.smoothing", lights.ProjectedShadowSmoothing);
    lights.ProjectedShadowFallbackDirection = Vec<3>(
        ReadCVarFloat(console, "render.shadow.projected.dir_x",
                      lights.ProjectedShadowFallbackDirection.X),
        ReadCVarFloat(console, "render.shadow.projected.dir_y",
                      lights.ProjectedShadowFallbackDirection.Y),
        ReadCVarFloat(console, "render.shadow.projected.dir_z",
                      lights.ProjectedShadowFallbackDirection.Z));
}
