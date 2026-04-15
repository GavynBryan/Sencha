#include <gtest/gtest.h>
#include <assets/texture/Image.h>
#include <assets/texture/ImageLoader.h>

// ============================================================================
// Image struct
// ============================================================================

TEST(Image, DefaultIsInvalid)
{
    Image img;
    EXPECT_FALSE(img.IsValid());
    EXPECT_EQ(img.Width, 0u);
    EXPECT_EQ(img.Height, 0u);
}

TEST(Image, PopulatedIsValid)
{
    Image img;
    img.Width  = 4;
    img.Height = 4;
    img.Pixels.resize(img.Width * img.Height * 4u, 0xFF);
    EXPECT_TRUE(img.IsValid());
}

TEST(Image, ByteSize)
{
    Image img;
    img.Width  = 16;
    img.Height = 8;
    img.Pixels.resize(img.Width * img.Height * 4u);
    EXPECT_EQ(img.ByteSize(), 16u * 8u * 4u);
}

TEST(Image, BytesPerPixelAlwaysFour)
{
    Image img;
    EXPECT_EQ(img.BytesPerPixel(), 4u);
}

TEST(Image, DefaultFormatIsSRGB)
{
    Image img;
    EXPECT_EQ(img.Format, PixelFormat::RGBA8_SRGB);
}

// ============================================================================
// LoadImageFromFile
// ============================================================================

TEST(ImageLoader, NonExistentFileReturnsNullopt)
{
    auto result = LoadImageFromFile("this_file_does_not_exist.png");
    EXPECT_FALSE(result.has_value());
}

// ============================================================================
// LoadImageFromMemory
//
// Uses a minimal 1x1 white pixel BMP (24-bit, top-down) embedded as raw bytes.
// BMP's uncompressed pixel layout makes it verifiable without compression math.
// After stb_image forces 4-channel RGBA, the single pixel should be white
// (0xFF, 0xFF, 0xFF, 0xFF).
// ============================================================================

// clang-format off
static const uint8_t kWhite1x1Bmp[] = {
    // BMP file header (14 bytes)
    0x42, 0x4D,             // "BM" signature
    0x3A, 0x00, 0x00, 0x00, // file size = 58
    0x00, 0x00, 0x00, 0x00, // reserved
    0x36, 0x00, 0x00, 0x00, // pixel data offset = 54

    // BITMAPINFOHEADER (40 bytes)
    0x28, 0x00, 0x00, 0x00, // header size = 40
    0x01, 0x00, 0x00, 0x00, // width  = 1
    0xFF, 0xFF, 0xFF, 0xFF, // height = -1 (negative => top-down)
    0x01, 0x00,             // color planes = 1
    0x18, 0x00,             // bits per pixel = 24
    0x00, 0x00, 0x00, 0x00, // compression = BI_RGB
    0x04, 0x00, 0x00, 0x00, // image size = 4 (3 bytes + 1 padding)
    0x00, 0x00, 0x00, 0x00, // X pixels per meter
    0x00, 0x00, 0x00, 0x00, // Y pixels per meter
    0x00, 0x00, 0x00, 0x00, // colors in table
    0x00, 0x00, 0x00, 0x00, // important colors

    // Pixel data (BGR order, padded to 4 bytes)
    0xFF, 0xFF, 0xFF,       // white pixel (B=255, G=255, R=255)
    0x00                    // row padding
};
// clang-format on

TEST(ImageLoader, LoadFromMemoryReturnsValidImage)
{
    auto result = LoadImageFromMemory(kWhite1x1Bmp, static_cast<int>(sizeof(kWhite1x1Bmp)));
    ASSERT_TRUE(result.has_value());

    const Image& img = *result;
    EXPECT_EQ(img.Width, 1u);
    EXPECT_EQ(img.Height, 1u);
    EXPECT_TRUE(img.IsValid());
    EXPECT_EQ(img.ByteSize(), 4u);
}

TEST(ImageLoader, LoadFromMemoryProducesRGBA)
{
    auto result = LoadImageFromMemory(kWhite1x1Bmp, static_cast<int>(sizeof(kWhite1x1Bmp)));
    ASSERT_TRUE(result.has_value());

    const auto& pixels = result->Pixels;
    ASSERT_EQ(pixels.size(), 4u);
    EXPECT_EQ(pixels[0], 0xFF); // R
    EXPECT_EQ(pixels[1], 0xFF); // G
    EXPECT_EQ(pixels[2], 0xFF); // B
    EXPECT_EQ(pixels[3], 0xFF); // A (added by stb_image STBI_rgb_alpha)
}

TEST(ImageLoader, LoadFromMemorySRGBFlag)
{
    auto srgb   = LoadImageFromMemory(kWhite1x1Bmp, static_cast<int>(sizeof(kWhite1x1Bmp)), true);
    auto linear = LoadImageFromMemory(kWhite1x1Bmp, static_cast<int>(sizeof(kWhite1x1Bmp)), false);

    ASSERT_TRUE(srgb.has_value());
    ASSERT_TRUE(linear.has_value());
    EXPECT_EQ(srgb->Format,   PixelFormat::RGBA8_SRGB);
    EXPECT_EQ(linear->Format, PixelFormat::RGBA8);

    // Pixel bytes are identical regardless of the format tag
    EXPECT_EQ(srgb->Pixels, linear->Pixels);
}

TEST(ImageLoader, LoadFromMemoryNullDataReturnsNullopt)
{
    auto result = LoadImageFromMemory(nullptr, 0);
    EXPECT_FALSE(result.has_value());
}

TEST(ImageLoader, LoadFromMemoryCorruptDataReturnsNullopt)
{
    const uint8_t garbage[] = { 0xDE, 0xAD, 0xBE, 0xEF };
    auto result = LoadImageFromMemory(garbage, static_cast<int>(sizeof(garbage)));
    EXPECT_FALSE(result.has_value());
}
