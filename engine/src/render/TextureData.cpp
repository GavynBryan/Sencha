#include <render/TextureData.h>

bool ValidateTextureData(const TextureData& texture)
{
    if (!texture.IsValid())
        return false;

    if (texture.Mips[0].Width != texture.Width || texture.Mips[0].Height != texture.Height)
        return false;

    uint64_t expectedOffset = 0;
    uint32_t expectedWidth = texture.Width;
    uint32_t expectedHeight = texture.Height;

    for (const TextureMipLevel& mip : texture.Mips)
    {
        if (mip.Width != expectedWidth || mip.Height != expectedHeight)
            return false;
        if (mip.Offset != expectedOffset)
            return false;
        if (mip.ByteSize != TextureMipByteSize(texture.Format, mip.Width, mip.Height))
            return false;

        expectedOffset += mip.ByteSize;
        expectedWidth = expectedWidth > 1 ? expectedWidth / 2 : 1;
        expectedHeight = expectedHeight > 1 ? expectedHeight / 2 : 1;
    }

    return expectedOffset == texture.Blob.size();
}
