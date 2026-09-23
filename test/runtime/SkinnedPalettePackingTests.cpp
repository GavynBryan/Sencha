// The pose shader reads each palette entry as a GLSL mat4 in a std430 buffer,
// which is column-major; the CPU builds row-major Mat4. The upload converts.
// These pin the whole layout -- every element and what the rebuilt matrix does
// to a point and a direction -- so a memcpy, or a transpose that gets only
// the translation right, cannot pass.

#include <gtest/gtest.h>

#include <math/Quat.h>
#include <math/geometry/3d/Transform3d.h>
#include <render/SkinnedPoseFrameData.h>

#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace
{
    // A palette entry with a 3x3 that is not symmetric, so its transpose is a
    // different matrix: translation, two rotations about different axes, and
    // non-uniform scale.
    Mat4 AsymmetricPaletteEntry()
    {
        const float deg = std::numbers::pi_v<float> / 180.0f;
        const Quatf rotation = Quatf::FromAxisAngle(Vec3d(1.0f, 0.0f, 0.0f), 30.0f * deg)
            * Quatf::FromAxisAngle(Vec3d(0.0f, 0.0f, 1.0f), 50.0f * deg);
        return Transform3f(Vec3d(1.0f, 2.0f, 3.0f), rotation, Vec3d(1.0f, 2.0f, 3.0f)).ToMat4();
    }

    // What GLSL does with a column-major mat4 m and a vec4 v: m * v, where
    // m[col][row] = floats[col * 4 + row].
    std::array<float, 4> GlslMultiply(const float* floats, std::array<float, 4> v)
    {
        std::array<float, 4> out{};
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 4; ++col)
                out[static_cast<std::size_t>(row)] += floats[col * 4 + row] * v[static_cast<std::size_t>(col)];
        return out;
    }
}

TEST(SkinnedPalettePacking, EveryElementLandsWhereTheShaderReadsIt)
{
    const Mat4 entry = AsymmetricPaletteEntry();
    ASSERT_NE(entry, entry.Transposed());
    const std::vector<Mat4> palettes{ Mat4::Identity(), entry };
    std::vector<float> packed(palettes.size() * kPaletteMatrixFloats, -99.0f);

    CopyPalettesColumnMajor(palettes, packed);

    const Mat4 expected = entry.Transposed();
    const float* second = packed.data() + kPaletteMatrixFloats;
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col)
            EXPECT_FLOAT_EQ(second[row * 4 + col], expected.Data[row][col]) << "element " << row * 4 + col;
    for (int i = 0; i < 16; ++i)
        EXPECT_FLOAT_EQ(packed[static_cast<std::size_t>(i)], (i % 5 == 0) ? 1.0f : 0.0f) << "identity element " << i;
}

TEST(SkinnedPalettePacking, TheShaderTransformsAsTheCpuDoes)
{
    const Mat4 entry = AsymmetricPaletteEntry();
    std::vector<float> packed(kPaletteMatrixFloats);
    CopyPalettesColumnMajor(std::span<const Mat4>(&entry, 1), packed);

    const Vec3d point(0.3f, -1.7f, 2.9f);
    const Vec3d cpuPoint = entry.TransformPoint(point);
    const std::array<float, 4> gpuPoint = GlslMultiply(packed.data(), { point.X, point.Y, point.Z, 1.0f });
    EXPECT_NEAR(gpuPoint[0], cpuPoint.X, 1e-5f);
    EXPECT_NEAR(gpuPoint[1], cpuPoint.Y, 1e-5f);
    EXPECT_NEAR(gpuPoint[2], cpuPoint.Z, 1e-5f);
    EXPECT_NEAR(gpuPoint[3], 1.0f, 1e-6f);

    const Vec3d direction(1.0f, 2.0f, -0.5f);
    const Vec3d cpuDirection = entry.TransformVector(direction);
    const std::array<float, 4> gpuDirection =
        GlslMultiply(packed.data(), { direction.X, direction.Y, direction.Z, 0.0f });
    EXPECT_NEAR(gpuDirection[0], cpuDirection.X, 1e-5f);
    EXPECT_NEAR(gpuDirection[1], cpuDirection.Y, 1e-5f);
    EXPECT_NEAR(gpuDirection[2], cpuDirection.Z, 1e-5f);
}
