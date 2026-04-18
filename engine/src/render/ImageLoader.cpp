#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <render/ImageLoader.h>

#include <string>

std::optional<Image> LoadImageFromFile(std::string_view path, bool srgb)
{
    const std::string pathStr(path);

    int w = 0;
    int h = 0;
    int channels = 0;
    stbi_uc* data = stbi_load(pathStr.c_str(), &w, &h, &channels, STBI_rgb_alpha);
    if (!data)
        return std::nullopt;

    Image img;
    img.Width  = static_cast<uint32_t>(w);
    img.Height = static_cast<uint32_t>(h);
    img.Format = srgb ? PixelFormat::RGBA8_SRGB : PixelFormat::RGBA8;

    const size_t byteSize = static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;
    img.Pixels.assign(data, data + byteSize);

    stbi_image_free(data);
    return img;
}

std::optional<Image> LoadImageFromMemory(const uint8_t* data, int byteLen, bool srgb)
{
    int w = 0;
    int h = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(data, byteLen, &w, &h, &channels, STBI_rgb_alpha);
    if (!pixels)
        return std::nullopt;

    Image img;
    img.Width  = static_cast<uint32_t>(w);
    img.Height = static_cast<uint32_t>(h);
    img.Format = srgb ? PixelFormat::RGBA8_SRGB : PixelFormat::RGBA8;

    const size_t byteSize = static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;
    img.Pixels.assign(pixels, pixels + byteSize);

    stbi_image_free(pixels);
    return img;
}
