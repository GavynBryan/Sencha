#include <core/io/FileBytes.h>

#include <fstream>
#include <ios>

bool ReadFileBytes(const std::filesystem::path& path, std::vector<std::byte>& out)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
        return false;

    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size < 0)
        return false;
    file.seekg(0, std::ios::beg);

    out.resize(static_cast<std::size_t>(size));
    if (size > 0)
        file.read(reinterpret_cast<char*>(out.data()),
                  static_cast<std::streamsize>(size));
    return file.good() || out.empty();
}

bool WriteFileBytes(const std::filesystem::path& path, std::span<const std::byte> bytes)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
        return false;
    if (!bytes.empty())
        file.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    return file.good();
}
