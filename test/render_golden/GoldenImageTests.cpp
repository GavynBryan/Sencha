// Does the renderer still produce the same picture?
//
// Everything else in this suite runs without a device, which is right for
// policy and blind to the one thing a renderer exists to produce. A whole class
// of defect leaves every counter correct and every assertion green and shows up
// only in the image: a material setting parsed, validated, round-tripped, and
// never reaching a shader; two paths that should agree drifting apart; a pass
// drawing the right geometry through the wrong state. Those were found by a
// person looking at captures, which means they were found at whatever rate
// someone happened to look.
//
// This is the detector. It renders a scene the same way twice -- once now, once
// when the reference was recorded -- and compares. The comparison can be exact
// because the renderer is bit-deterministic frame to frame on one build:
// measured, not assumed, three runs of this scene produced byte-identical
// files.
//
// When it fails, one of two things is true, and only a person can say which:
// something broke, or the change was meant to alter the image. For the second,
// look at the .actual.png written beside the reference, then replace the
// reference with it in the same commit as the change that moved it.
//
// It skips rather than fails without a GPU or a display, so a machine that
// cannot run it says so instead of blocking.

#include <gtest/gtest.h>

#include <DeviceTestSupport.h>

#include <assets/texture/Image.h>
#include <assets/texture/ImageLoader.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{

// Frame 150 of 300: late enough that the window, the swapchain, and asset
// streaming have all settled, early enough to keep the run near three seconds.
constexpr int kCaptureFrame = 150;
constexpr int kRunFrames = 300;

struct GoldenScene
{
    const char* Name;
    const char* Map;
    // Cook through the editor rather than the assetless level cook. A level
    // that places assets by handle needs the asset-full cook: handles cannot
    // reproduce their paths without their stores, and the editor is the
    // process composed with all of them.
    bool EditorCook = false;

    // Extra startup cvars, appended verbatim. A scene that needs the host to
    // do something beyond loading a map says so here rather than growing a
    // boolean per capability.
    const char* ExtraArgs = "";
};

[[nodiscard]] std::filesystem::path ReferenceDir()
{
    return std::filesystem::path(SENCHA_GOLDEN_REFERENCE_DIR);
}

// The runtime loads only cooked scenes, and .cooked/ is gitignored -- nothing
// populates it, so a test that assumes it exists passes only on machines with
// local leftovers. Cook the fixture on every run instead: it costs well under
// a second per scene, and it puts the cook inside the net, which is where it
// belongs -- a cook change that moves the image should fail this test.
// Fixtures the scenes place (the skinned rig) regenerate before every run, so
// the artifacts always match the current format version instead of rotting as
// committed bytes.
[[nodiscard]] bool GenerateFixtures()
{
    const std::string command =
        std::string("SENCHA_SKINNED_FIXTURE_ROOT=") + SENCHA_GOLDEN_CONTENT_ROOT + "/assets "
        + SENCHA_GOLDEN_COOK + " --gtest_filter=SkinnedFixture.Generate >/dev/null 2>&1";
    std::system(command.c_str());
    return std::filesystem::exists(std::filesystem::path(SENCHA_GOLDEN_CONTENT_ROOT)
                                   / "assets" / "meshes" / "dev" / "golden_rig.skmesh");
}

// Source assets the scenes reference but the level cook does not touch: the
// authored UI documents above all. The assetless cook deliberately cooks a
// level and nothing else, so anything living beside it has to be imported
// separately or it never reaches the runtime.
[[nodiscard]] bool ImportContent()
{
    const std::string command =
        std::string("SENCHA_CONTENT_IMPORT_ROOT=") + SENCHA_GOLDEN_CONTENT_ROOT + "/assets "
        + SENCHA_GOLDEN_COOK + " --gtest_filter=ContentImport.Generate >/dev/null 2>&1";
    std::system(command.c_str());
    return std::filesystem::exists(std::filesystem::path(SENCHA_GOLDEN_CONTENT_ROOT)
                                   / "assets" / ".cooked" / "ui" / "golden.rml.sui");
}

[[nodiscard]] bool CookScene(const GoldenScene& scene)
{
    const std::string authored = std::string(SENCHA_GOLDEN_CONTENT_ROOT) + "/assets/"
        + scene.Map + ".sscene";
    const std::string command = scene.EditorCook
        ? std::string("SENCHA_PRESENT_MODE=IMMEDIATE ") + SENCHA_GOLDEN_EDITOR
            + " --project " + SENCHA_GOLDEN_CONTENT_ROOT + "/project.senchaproj"
            + " +editor.open " + authored
            + " +cook full +set app.exit_after_frames 3000 >/dev/null 2>&1"
        : std::string("SENCHA_COOK_LEVEL=") + authored
            + " SENCHA_COOK_ROOT=" + SENCHA_GOLDEN_CONTENT_ROOT + "/assets "
            + SENCHA_GOLDEN_COOK + " --gtest_filter=CookLevel.Generate >/dev/null 2>&1";
    std::system(command.c_str());

    const std::filesystem::path cooked = std::filesystem::path(SENCHA_GOLDEN_CONTENT_ROOT)
        / "assets" / ".cooked" / (std::string(scene.Map) + ".smap");
    return std::filesystem::exists(cooked);
}

// Renders `scene` and writes the capture to `output`. False when the run did
// not produce a file, which is a failed render rather than a changed image.
[[nodiscard]] bool RenderScene(const GoldenScene& scene, const std::filesystem::path& output)
{
    std::error_code ignored;
    std::filesystem::remove(output, ignored);

    // IMMEDIATE so the run is not paced by the display's refresh rate; the
    // capture names a frame, so presentation timing does not affect what it is.
    const std::string command =
        std::string("cd ") + SENCHA_GOLDEN_CONTENT_ROOT
        + " && SENCHA_PRESENT_MODE=IMMEDIATE " + SENCHA_GOLDEN_APP
        // A reference image is of the scene, not of whatever window mode or
        // frame cap the developer running the suite last saved.
        + " --settings none"
        + " +map " + scene.Map
        + " +render.screenshot " + output.string() + " " + std::to_string(kCaptureFrame)
        + " +set app.exit_after_frames " + std::to_string(kRunFrames)
        + " " + scene.ExtraArgs
        + " >/dev/null 2>&1";
    std::system(command.c_str());
    return std::filesystem::exists(output);
}

struct Comparison
{
    bool SameSize = false;
    std::size_t DifferingPixels = 0;
    std::size_t TotalPixels = 0;
    // Differing channels by how many 8-bit levels they moved: 1, 2, 3-4, 5-8,
    // 9-16, 17 or more. For the person deciding whether a change is a rounding
    // shift or a real one; it never affects the verdict, which stays exact.
    std::array<std::size_t, 6> LevelHistogram{};
    int LargestLevelDifference = 0;
    // Mean signed difference per channel, actual minus reference.
    std::array<double, 3> MeanSignedDifference{};
};

// `firstRow` skips the rows above it, for a capture whose top shows a machine's paths.
[[nodiscard]] Comparison Compare(const Image& reference, const Image& actual, std::size_t firstRow = 0)
{
    Comparison result;
    result.SameSize = reference.Width == actual.Width && reference.Height == actual.Height;
    if (!result.SameSize)
        return result;

    result.TotalPixels = static_cast<std::size_t>(reference.Width) * reference.Height;
    std::array<std::int64_t, 3> signedTotal{};
    for (std::size_t pixel = firstRow * reference.Width; pixel < result.TotalPixels; ++pixel)
    {
        const std::size_t byte = pixel * 4;
        // Alpha is forced opaque on capture, so three channels is the whole
        // comparison.
        bool differs = false;
        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            const int difference = static_cast<int>(actual.Pixels[byte + channel])
                                 - static_cast<int>(reference.Pixels[byte + channel]);
            if (difference == 0)
                continue;
            differs = true;
            signedTotal[channel] += difference;
            const int magnitude = std::abs(difference);
            result.LargestLevelDifference = std::max(result.LargestLevelDifference, magnitude);
            const std::size_t bucket = magnitude == 1 ? 0 : magnitude == 2 ? 1 : magnitude <= 4 ? 2
                                     : magnitude <= 8 ? 3 : magnitude <= 16 ? 4 : 5;
            ++result.LevelHistogram[bucket];
        }
        if (differs)
            ++result.DifferingPixels;
    }
    for (std::size_t channel = 0; channel < 3; ++channel)
        result.MeanSignedDifference[channel] =
            static_cast<double>(signedTotal[channel]) / static_cast<double>(result.TotalPixels);
    return result;
}

void CheckScene(const GoldenScene& scene)
{
    if (!DisplayAvailable())
        GTEST_SKIP() << "no display; the golden images need a GPU to render against";

    const std::filesystem::path reference = ReferenceDir() / (std::string(scene.Name) + ".png");
    const std::filesystem::path actual =
        ReferenceDir() / (std::string(scene.Name) + ".actual.png");

    ASSERT_TRUE(GenerateFixtures())
        << "the skinned fixture did not generate, so the scenes cannot reference it";
    ASSERT_TRUE(ImportContent())
        << "the content root's source assets did not import, so a scene that "
           "references one would render without it and quietly differ";
    ASSERT_TRUE(CookScene(scene))
        << "the level did not cook, so nothing below this describes the renderer";
    ASSERT_TRUE(RenderScene(scene, actual))
        << "the run produced no capture at all, so the renderer failed rather than changed";

    const std::optional<Image> actualImage = LoadImageFromFile(actual.string());
    ASSERT_TRUE(actualImage.has_value()) << "could not decode the capture at " << actual;

    if (!std::filesystem::exists(reference))
    {
        FAIL() << "no reference image for '" << scene.Name << "'. The capture is at "
               << actual << "; if it looks right, commit it as " << reference;
    }

    const std::optional<Image> referenceImage = LoadImageFromFile(reference.string());
    ASSERT_TRUE(referenceImage.has_value()) << "could not decode the reference at " << reference;

    const Comparison comparison = Compare(*referenceImage, *actualImage);
    ASSERT_TRUE(comparison.SameSize)
        << "reference is " << referenceImage->Width << "x" << referenceImage->Height
        << " and the capture is " << actualImage->Width << "x" << actualImage->Height;

    EXPECT_EQ(comparison.DifferingPixels, 0u)
        << comparison.DifferingPixels << " of " << comparison.TotalPixels << " pixels differ ("
        << (100.0 * static_cast<double>(comparison.DifferingPixels)
            / static_cast<double>(comparison.TotalPixels))
        << "%), by at most " << comparison.LargestLevelDifference << " levels. Channels moved by "
        << "1: " << comparison.LevelHistogram[0] << ", 2: " << comparison.LevelHistogram[1]
        << ", 3-4: " << comparison.LevelHistogram[2] << ", 5-8: " << comparison.LevelHistogram[3]
        << ", 9-16: " << comparison.LevelHistogram[4] << ", 17+: " << comparison.LevelHistogram[5]
        << ". Mean signed difference RGB: " << comparison.MeanSignedDifference[0] << ", "
        << comparison.MeanSignedDifference[1] << ", " << comparison.MeanSignedDifference[2]
        << ". Compare " << actual << " against " << reference << ". If the change was "
        << "meant to alter the image, replace the reference with the capture in the same "
        << "commit; otherwise something moved that should not have.";

    if (comparison.DifferingPixels == 0)
    {
        std::error_code ignored;
        std::filesystem::remove(actual, ignored);
    }
}

// What the output pass should write for a surface emitting `emitted` linear light,
// worked out here rather than taken from a capture, so it cannot agree with a
// transform applied twice or not at all.
[[nodiscard]] int ExpectedDisplayLevel(float emitted, float exposure)
{
    constexpr float kKnee = 0.8f;
    float display = emitted * exposure;
    if (display > kKnee)
    {
        const float s = (display - kKnee) / (1.0f - kKnee);
        display = kKnee + (1.0f - kKnee) * s / (1.0f + s);
    }
    const float encoded = display <= 0.0031308f ? display * 12.92f
                                                : 1.055f * std::pow(display, 1.0f / 2.4f) - 0.055f;
    return static_cast<int>(std::lround(encoded * 255.0f));
}

// Below 1, `coverage` blends the black surface over a black sky, where its emission
// must land exactly where an opaque surface's does.
void CheckEmittedLevel(float emitted, float exposure, float coverage = 1.0f)
{
    if (!DisplayAvailable())
        GTEST_SKIP() << "no display; the golden images need a GPU to render against";

    const bool blended = coverage < 1.0f;
    const std::filesystem::path material = std::filesystem::path(SENCHA_GOLDEN_CONTENT_ROOT) / "assets" / "materials"
        / "dev" / "generated_tonemap_ramp.smat";
    std::ofstream(material, std::ios::trunc)
        << "{ \"id\": \"7a3f10c2b95e4d18\", \"version\": 2, \"shading\": \"unlit\", "
        << (blended ? "\"alpha_mode\": \"blend\", " : "")
        << "\"base_color_factor\": [0, 0, 0, " << coverage << "], \"emissive_factor\": [1, 1, 1], "
        << "\"emissive_strength\": " << emitted << " }\n";

    std::string args = "+set render.exposure " + std::to_string(exposure);
    if (blended)
        for (const char* channel : { "sky_r", "sky_g", "sky_b", "ground_r", "ground_g", "ground_b" })
            args += std::string(" +set render.ambient.") + channel + " 0";
    const GoldenScene scene{ .Name = "tonemap_ramp", .Map = "levels/golden_tonemap_ramp", .ExtraArgs = args.c_str() };
    ASSERT_TRUE(CookScene(scene));
    const std::filesystem::path capture = std::filesystem::temp_directory_path() / "sencha_tonemap_ramp.png";
    ASSERT_TRUE(RenderScene(scene, capture));
    const std::optional<Image> image = LoadImageFromFile(capture.string());
    ASSERT_TRUE(image.has_value());

    const std::size_t pixel = (static_cast<std::size_t>(image->Height / 2) * image->Width + image->Width / 2) * 4;
    const int expected = ExpectedDisplayLevel(emitted, exposure);
    for (std::size_t channel = 0; channel < 3; ++channel)
        EXPECT_NEAR(static_cast<int>(image->Pixels[pixel + channel]), expected, 1)
            << "a surface emitting " << emitted << " at exposure " << exposure << ", channel " << channel;
}

} // namespace

// Two overlapping styles with the higher priority winning their shared edge, a mesh
// partly behind an unoutlined wall, and an alpha-masked mesh outlined along its cutout.
TEST(GoldenImage, OutlinesFollowPriorityOcclusionAndCutouts)
{
    CheckScene({ .Name = "outlines", .Map = "levels/golden_outline", .EditorCook = true,
                 .ExtraArgs = "+set render_host.outline 1" });
}

// A display effect covers outlines like the rest of the frame: a half fade dims
// them, where drawn after the chain they would stay at full strength.
TEST(GoldenImage, AFadeCoversOutlinesToo)
{
    CheckScene({ .Name = "outline_fade", .Map = "levels/golden_outline", .EditorCook = true,
                 .ExtraArgs = "+set render_host.outline 1 +set render_host.post.fade 0.5" });
}

// The scissor around the highlights clamps at every edge of the view.
TEST(GoldenImage, AnOutlineIsClippedByTheViewNotByItsScissor)
{
    CheckScene({ .Name = "outline_edges", .Map = "levels/golden_outline_edges", .EditorCook = true,
                 .ExtraArgs = "+set render_host.outline 1" });
}

// A mesh reaching past the camera has no meaningful screen bounds, so it takes the view.
TEST(GoldenImage, AnOutlineOnAMeshBehindTheCameraIsNotLost)
{
    CheckScene({ .Name = "outline_behind", .Map = "levels/golden_outline_behind", .EditorCook = true,
                 .ExtraArgs = "+set render_host.outline 1" });
}

// The posed rig, because a skinned mesh binds its posed buffer in the mask too.
TEST(GoldenImage, AnOutlineFollowsTheSkinnedPose)
{
    CheckScene({ .Name = "outline_skinned", .Map = "levels/golden_skinned_pose", .EditorCook = true,
                 .ExtraArgs = "+set render_host.outline 1" });
}

// An effect at weight 0 is not scheduled, so the frame is the plain scene's byte
// for byte; the other two show the chain running before and after the transform.
TEST(GoldenImage, AnEffectAtWeightZeroIsNotScheduled)
{
    CheckScene({ .Name = "shadow_probe", .Map = "levels/shadow_probe", .ExtraArgs = "+set render_host.post.fade 0" });
}

TEST(GoldenImage, ADisplayEffectRunsAfterTheTransform)
{
    CheckScene({ .Name = "post_fade", .Map = "levels/shadow_probe", .ExtraArgs = "+set render_host.post.fade 0.5" });
}

TEST(GoldenImage, ASceneEffectRunsBeforeTheTransform)
{
    CheckScene({ .Name = "post_displace", .Map = "levels/shadow_probe", .ExtraArgs = "+set render_host.post.displace 1" });
}

// Kyusu's own path: the placed mesh the selection names gets the outline and its
// neighbour does not, through the same mask family and composite as the runtime. Below
// the title bar and breadcrumb, which show this checkout's path.
namespace
{
// Kyusu on a fixture level, compared below the rows that show the machine's paths.
void CheckEditorCapture(const std::string& level, const std::string& commands, const std::string& name)
{
    constexpr std::size_t kBelowPathRows = 80;
    const std::filesystem::path reference = ReferenceDir() / (name + ".png");
    const std::filesystem::path actual = ReferenceDir() / (name + ".actual.png");
    std::error_code ignored;
    std::filesystem::remove(actual, ignored);
    const std::string root = SENCHA_GOLDEN_CONTENT_ROOT;
    const std::string command = std::string("SENCHA_PRESENT_MODE=IMMEDIATE ") + SENCHA_GOLDEN_EDITOR
        + " --settings none --project " + root + "/project.senchaproj"
        // The window is real, so without this a pointer or wheel at the desk reaches it.
        + " +input.exclusive"
        + " +editor.open " + root + "/assets/levels/" + level + commands
        + " +render.screenshot " + actual.string() + " 400"
        + " +set app.exit_after_frames 500 >/dev/null 2>&1";
    std::system(command.c_str());

    const std::optional<Image> actualImage = LoadImageFromFile(actual.string());
    const std::optional<Image> referenceImage = LoadImageFromFile(reference.string());
    ASSERT_TRUE(actualImage.has_value()) << "the editor produced no capture";
    ASSERT_TRUE(referenceImage.has_value());
    const Comparison comparison = Compare(*referenceImage, *actualImage, kBelowPathRows);
    ASSERT_TRUE(comparison.SameSize);
    EXPECT_EQ(comparison.DifferingPixels, 0u)
        << comparison.DifferingPixels << " pixels differ, by at most " << comparison.LargestLevelDifference
        << " levels. Compare " << actual << " against " << reference;
    if (comparison.DifferingPixels == 0)
        std::filesystem::remove(actual, ignored);
}
} // namespace

TEST(GoldenImage, ASelectedPlacedMeshIsOutlinedInAViewport)
{
    if (!DisplayAvailable())
        GTEST_SKIP() << "no display; the golden images need a GPU to render against";
    CheckEditorCapture("golden_editor_selection.sscene", " +editor.select 05ade20000000001", "editor_selection");
}

// The ortho views draw the flat backdrop; a translucent surface over it must still
// blend over it rather than be painted out.
TEST(GoldenImage, AGlassMeshShowsOverTheOrthoBackdrop)
{
    if (!DisplayAvailable())
        GTEST_SKIP() << "no display; the golden images need a GPU to render against";
    CheckEditorCapture("golden_editor_ortho_glass.sscene", " +set editor.view.arrangement 2", "editor_ortho_glass");
}

// The display transform against its own formula, through the forward pass and the
// output pass of the shipped host. It catches a transform applied twice or not at
// all whatever the other references say.
TEST(TonemapRamp, EachEmittedLevelLandsWhereTheFormulaSays)
{
    for (const float emitted : { 0.0f, 0.25f, 0.75f, 1.0f, 1.5f, 2.0f })
        CheckEmittedLevel(emitted, 1.0f);
}

TEST(TonemapRamp, ExposureScalesBeforeTheShoulder)
{
    CheckEmittedLevel(0.5f, 2.0f);
}

TEST(TonemapRamp, ABlendedSurfaceEmitsAtFullStrength)
{
    for (const float emitted : { 0.25f, 1.0f, 2.0f })
        CheckEmittedLevel(emitted, 1.0f, 0.1f);
}

// One scene covering as much of the renderer as a single frame can: cooked
// meshes and materials, the forward pass, spot and point shadows, the ambient
// hemisphere, the sky gradient, and the display transform.
TEST(GoldenImage, ShadowProbeSceneIsUnchanged)
{
    CheckScene({ .Name = "shadow_probe", .Map = "levels/shadow_probe" });
}

// Debug lines from a simulation-side channel through extraction to the line
// pass: a square the scene's geometry may hide and a cross drawn over it.
TEST(GoldenImage, DebugLinesDrawOverTheScene)
{
    CheckScene({ .Name = "debug_lines", .Map = "levels/shadow_probe",
                 .ExtraArgs = "+set render_host.debug_lines 1" });
}

// A rest-pose skinned mesh in a cooked level -- the P5 gate, permanent. The
// red two-box column resolves through SkinnedMeshCache: component, codec,
// cook, load, retain, extraction, and draw all have to hold for it to appear.
TEST(GoldenImage, ARestPoseSkinnedMeshDraws)
{
    CheckScene({ .Name = "skinned_rest", .Map = "levels/golden_skinned",
                 .EditorCook = true });
}

// The pose gate: a one-layer rig holds a one-second bend at 0.5s, a fixed pose
// the tick accumulator cannot vary, so every stage from clip to posed draw must
// tilt the upper box an eighth turn about its joint.
TEST(GoldenImage, AClipPosesASkinnedMesh)
{
    CheckScene({ .Name = "skinned_pose", .Map = "levels/golden_skinned_pose",
                 .EditorCook = true });
}

// Authored UI over a rendered scene: the first-pixels gate.
//
// Four panels chosen for what they can each break on their own. An opaque one
// says geometry, the surface projection, and the colour conversion all agree.
// A translucent one overlapping it says premultiplied-alpha blending is right
// where it is only visible -- where two things cross. A rounded one is the
// shape authored chrome is actually made of. And a fully transparent one over
// the opaque panel must vanish completely: if premultiplication is inverted
// anywhere along the path, it darkens what is underneath instead.
//
// The host is the second half of the proof. It builds its own UiService over
// the asset caches, drives update and extraction from its own system, and
// stages the render feature itself -- so this also says a plain Sencha
// application can put authored UI on screen without engine privilege.
TEST(GoldenImage, AuthoredUiDrawsOverTheScene)
{
    CheckScene({ .Name = "authored_ui", .Map = "levels/shadow_probe",
                 .ExtraArgs = "+set render_host.ui asset://ui/golden.rml" });
}

// A host image: pixels the host chooses at runtime, drawn where a document names
// them. The host leases a texture and publishes it by its bindless slot, the
// path an editor's views, thumbnails and previews take into authored UI.
TEST(GoldenImage, AHostImageDrawsWhereTheDocumentNamesIt)
{
    CheckScene({ .Name = "host_image", .Map = "levels/shadow_probe",
                 .ExtraArgs = "+set render_host.ui.picture asset://textures/dev/checker.png "
                              "+set render_host.ui asset://ui/golden_host.rml" });
}

// The same geometry through a blended default material: the transparent pass's
// pixel proof. If blend ever silently falls back to opaque again, this frame
// stops showing the background through the floor and the comparison fails.
TEST(GoldenImage, BlendedMaterialStillBlends)
{
    CheckScene({ .Name = "transparency", .Map = "levels/golden_transparency" });
}
