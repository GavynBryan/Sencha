#include <core/io/FileBytes.h>

#include <fstream>
#include <ios>
#include <system_error>

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

namespace
{
    std::filesystem::path StagingPathFor(const std::filesystem::path& path)
    {
        std::filesystem::path staged = path;
        staged += ".tmp";
        return staged;
    }

    bool WriteWhole(const std::filesystem::path& path, std::span<const std::byte> bytes)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
            return false;
        if (!bytes.empty())
            file.write(reinterpret_cast<const char*>(bytes.data()),
                       static_cast<std::streamsize>(bytes.size()));
        file.close();
        return !file.fail();
    }

    bool ReplaceWith(const std::filesystem::path& staged, const std::filesystem::path& path)
    {
        std::error_code ec;
        std::filesystem::rename(staged, path, ec);
        if (!ec)
            return true;
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(staged, path, ec);
        return !ec;
    }
}

bool WriteFileBytes(const std::filesystem::path& path, std::span<const std::byte> bytes)
{
    const std::filesystem::path staged = StagingPathFor(path);
    if (WriteWhole(staged, bytes) && ReplaceWith(staged, path))
        return true;
    std::error_code ignored;
    std::filesystem::remove(staged, ignored);
    return false;
}

bool WriteFileText(const std::filesystem::path& path, std::string_view text)
{
    return WriteFileBytes(path, std::as_bytes(std::span(text.data(), text.size())));
}
