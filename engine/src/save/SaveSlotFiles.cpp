#include <save/SaveSlotFiles.h>

#include <core/io/FileBytes.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <system_error>
#include <utility>

namespace
{
    std::filesystem::path BackupOf(std::filesystem::path path)
    {
        path += ".bak";
        return path;
    }

    std::optional<std::vector<std::byte>> ReadWhole(const std::filesystem::path& path)
    {
        std::vector<std::byte> bytes;
        if (!ReadFileBytes(path, bytes))
            return std::nullopt;
        return bytes;
    }
}

SaveSlotFiles::SaveSlotFiles(std::filesystem::path root)
    : Root(std::move(root))
{
}

bool SaveSlotFiles::IsSlotName(std::string_view slot)
{
    return !slot.empty() && slot.size() <= 64 && std::ranges::all_of(slot, [](unsigned char c) {
        return std::isalnum(c) != 0 || c == '-' || c == '_';
    });
}

std::filesystem::path SaveSlotFiles::PathOf(std::string_view slot) const
{
    return Root / (std::string(slot) + ".ssav");
}

bool SaveSlotFiles::Write(std::string_view slot, std::span<const std::byte> bytes) const
{
    std::error_code ec;
    std::filesystem::create_directories(Root, ec);
    return WriteFileBytes(PathOf(slot), bytes);
}

bool SaveSlotFiles::WriteBackup(std::string_view slot, std::span<const std::byte> bytes) const
{
    std::error_code ec;
    std::filesystem::create_directories(Root, ec);
    return WriteFileBytes(BackupOf(PathOf(slot)), bytes);
}

std::optional<std::vector<std::byte>> SaveSlotFiles::Read(std::string_view slot) const
{
    return ReadWhole(PathOf(slot));
}

std::optional<std::vector<std::byte>> SaveSlotFiles::ReadBackup(std::string_view slot) const
{
    return ReadWhole(BackupOf(PathOf(slot)));
}
