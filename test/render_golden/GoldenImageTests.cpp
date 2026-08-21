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

#include <render/Image.h>
#include <render/ImageLoader.h>

#include <cstdlib>
#include <filesystem>
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
};

[[nodiscard]] std::filesystem::path ReferenceDir()
{
    return std::filesystem::path(SENCHA_GOLDEN_REFERENCE_DIR);
}

[[nodiscard]] bool DisplayAvailable()
{
    const char* display = std::getenv("DISPLAY");
    const char* wayland = std::getenv("WAYLAND_DISPLAY");
    return (display != nullptr && display[0] != '\0')
        || (wayland != nullptr && wayland[0] != '\0');
}

// The runtime loads only cooked scenes, and .cooked/ is gitignored -- nothing
// populates it, so a test that assumes it exists passes only on machines with
// local leftovers. Cook the fixture on every run instead: it costs well under
// a second per scene, and it puts the cook inside the net, which is where it
// belongs -- a cook change that moves the image should fail this test.
[[nodiscard]] bool CookScene(const GoldenScene& scene)
{
    const std::string command =
        std::string("SENCHA_COOK_LEVEL=") + SENCHA_GOLDEN_CONTENT_ROOT + "/assets/"
        + scene.Map + ".json"
        + " SENCHA_COOK_ROOT=" + SENCHA_GOLDEN_CONTENT_ROOT + "/assets "
        + SENCHA_GOLDEN_COOK + " --gtest_filter=CookLevel.Generate >/dev/null 2>&1";
    std::system(command.c_str());

    const std::filesystem::path cooked = std::filesystem::path(SENCHA_GOLDEN_CONTENT_ROOT)
        / "assets" / ".cooked" / (std::string(scene.Map) + ".cooked.json");
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
        + " +map " + scene.Map
        + " +render.screenshot " + output.string() + " " + std::to_string(kCaptureFrame)
        + " +set app.exit_after_frames " + std::to_string(kRunFrames)
        + " >/dev/null 2>&1";
    std::system(command.c_str());
    return std::filesystem::exists(output);
}

struct Comparison
{
    bool SameSize = false;
    std::size_t DifferingPixels = 0;
    std::size_t TotalPixels = 0;
};

[[nodiscard]] Comparison Compare(const Image& reference, const Image& actual)
{
    Comparison result;
    result.SameSize = reference.Width == actual.Width && reference.Height == actual.Height;
    if (!result.SameSize)
        return result;

    result.TotalPixels = static_cast<std::size_t>(reference.Width) * reference.Height;
    for (std::size_t pixel = 0; pixel < result.TotalPixels; ++pixel)
    {
        const std::size_t byte = pixel * 4;
        // Alpha is forced opaque on capture, so three channels is the whole
        // comparison.
        if (reference.Pixels[byte + 0] != actual.Pixels[byte + 0]
            || reference.Pixels[byte + 1] != actual.Pixels[byte + 1]
            || reference.Pixels[byte + 2] != actual.Pixels[byte + 2])
        {
            ++result.DifferingPixels;
        }
    }
    return result;
}

void CheckScene(const GoldenScene& scene)
{
    if (!DisplayAvailable())
        GTEST_SKIP() << "no display; the golden images need a GPU to render against";

    const std::filesystem::path reference = ReferenceDir() / (std::string(scene.Name) + ".png");
    const std::filesystem::path actual =
        ReferenceDir() / (std::string(scene.Name) + ".actual.png");

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
        << "%). Compare " << actual << " against " << reference << ". If the change was "
        << "meant to alter the image, replace the reference with the capture in the same "
        << "commit; otherwise something moved that should not have.";

    if (comparison.DifferingPixels == 0)
    {
        std::error_code ignored;
        std::filesystem::remove(actual, ignored);
    }
}

} // namespace

// One scene covering as much of the renderer as a single frame can: cooked
// meshes and materials, the forward pass, spot and point shadows, the ambient
// hemisphere, the sky gradient, and the display transform.
TEST(GoldenImage, ShadowProbeSceneIsUnchanged)
{
    CheckScene({ .Name = "shadow_probe", .Map = "levels/shadow_probe.level" });
}

// The same geometry through a blended default material: the transparent pass's
// pixel proof. If blend ever silently falls back to opaque again, this frame
// stops showing the background through the floor and the comparison fails.
TEST(GoldenImage, BlendedMaterialStillBlends)
{
    CheckScene({ .Name = "transparency", .Map = "levels/golden_transparency.level" });
}
