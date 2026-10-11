// The PNG encoder, through the engine's own reader: what goes in comes back.

#include <assets/texture/ImageLoader.h>
#include <assets/texture/PngEncoding.h>
#include <core/io/FileBytes.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <vector>

namespace
{
    std::vector<std::uint8_t> Checker(std::uint32_t width, std::uint32_t height)
    {
        std::vector<std::uint8_t> rgba;
        for (std::uint32_t y = 0; y < height; ++y)
            for (std::uint32_t x = 0; x < width; ++x)
            {
                const std::uint8_t level = ((x + y) % 2 == 0) ? 255 : 16;
                rgba.insert(rgba.end(), { level, static_cast<std::uint8_t>(x * 40),
                                          static_cast<std::uint8_t>(y * 40), 255 });
            }
        return rgba;
    }
}

TEST(PngEncoding, RoundTripsThroughTheImageReader)
{
    const std::vector<std::uint8_t> rgba = Checker(5, 3);
    const std::vector<std::byte> png = EncodePngRgba8(5, 3, rgba);
    ASSERT_FALSE(png.empty());

    const std::optional<Image> decoded = LoadImageFromMemory(
        reinterpret_cast<const std::uint8_t*>(png.data()), static_cast<int>(png.size()), false);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->Width, 5u);
    EXPECT_EQ(decoded->Height, 3u);
    EXPECT_EQ(decoded->Pixels, rgba);
}

TEST(PngEncoding, ASizeThatDisagreesWithThePixelsEncodesNothing)
{
    EXPECT_TRUE(EncodePngRgba8(4, 4, Checker(5, 3)).empty());
    EXPECT_TRUE(EncodePngRgba8(0, 3, {}).empty());
}

TEST(FileBytes, WhatIsWrittenReadsBack)
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "sencha_file_bytes_round_trip.bin";
    const std::vector<std::byte> bytes{ std::byte{ 1 }, std::byte{ 0 }, std::byte{ 255 } };
    ASSERT_TRUE(WriteFileBytes(path, bytes));
    std::vector<std::byte> read;
    ASSERT_TRUE(ReadFileBytes(path, read));
    EXPECT_EQ(read, bytes);
    std::filesystem::remove(path);
}
