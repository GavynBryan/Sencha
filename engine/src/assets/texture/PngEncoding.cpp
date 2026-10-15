#include <assets/texture/PngEncoding.h>

// The only translation unit that needs the writer, so it carries the
// implementation, matching how ImageLoader.cpp carries the reader's.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <cstring>

namespace
{
    void AppendBytes(void* context, void* data, int size)
    {
        auto& out = *static_cast<std::vector<std::byte>*>(context);
        const std::size_t start = out.size();
        out.resize(start + static_cast<std::size_t>(size));
        std::memcpy(out.data() + start, data, static_cast<std::size_t>(size));
    }
}

std::vector<std::byte> EncodePngRgba8(std::uint32_t width, std::uint32_t height,
                                      std::span<const std::uint8_t> rgba)
{
    std::vector<std::byte> png;
    if (width == 0 || height == 0
        || rgba.size() != static_cast<std::size_t>(width) * height * 4)
        return png;
    const int written = stbi_write_png_to_func(&AppendBytes, &png, static_cast<int>(width),
                                               static_cast<int>(height), 4, rgba.data(),
                                               static_cast<int>(width) * 4);
    if (written == 0)
        png.clear();
    return png;
}
