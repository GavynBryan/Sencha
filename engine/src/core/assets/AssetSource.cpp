#include <core/assets/AssetSource.h>

#include <fstream>
#include <string>

bool FileAssetSource::ReadBytes(std::string_view filePath, std::vector<std::byte>& out)
{
    std::ifstream file{ std::string(filePath), std::ios::binary | std::ios::ate };
    if (!file.is_open())
        return false;

    const std::streamsize size = file.tellg();
    if (size < 0)
        return false;

    out.resize(static_cast<std::size_t>(size));
    file.seekg(0, std::ios::beg);
    if (size > 0 && !file.read(reinterpret_cast<char*>(out.data()), size))
        return false;

    return true;
}

bool ReadAssetBytes(IAssetSource& source, const AssetRecord& record, std::vector<std::byte>& out)
{
    const std::string_view filePath =
        record.FilePath.empty() ? std::string_view(record.Path) : std::string_view(record.FilePath);
    return source.ReadBytes(filePath, out);
}
