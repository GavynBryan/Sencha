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

    std::optional<std::vector<std::byte>> ReadPrefixOf(const std::filesystem::path& path,
                                                       const std::function<std::size_t(std::span<const std::byte>)>& wanted)
    {
        std::vector<std::byte> bytes;
        if (!ReadFilePrefix(path, wanted, bytes))
            return std::nullopt;
        return bytes;
    }

    bool RemoveFile(const std::filesystem::path& path)
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        return !ec && !std::filesystem::exists(path, ec);
    }

    constexpr std::string_view kSlotExtension = ".ssav";
    constexpr std::string_view kBackupExtension = ".ssav.bak";
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

std::optional<std::vector<std::byte>>
SaveSlotFiles::ReadPrefix(std::string_view slot, const std::function<std::size_t(std::span<const std::byte>)>& wanted) const
{
    return ReadPrefixOf(PathOf(slot), wanted);
}

std::optional<std::vector<std::byte>>
SaveSlotFiles::ReadBackupPrefix(std::string_view slot,
                                const std::function<std::size_t(std::span<const std::byte>)>& wanted) const
{
    return ReadPrefixOf(BackupOf(PathOf(slot)), wanted);
}

bool SaveSlotFiles::Remove(std::string_view slot) const
{
    const bool file = RemoveFile(PathOf(slot));
    return RemoveBackup(slot) && file;
}

bool SaveSlotFiles::RemoveBackup(std::string_view slot) const
{
    return RemoveFile(BackupOf(PathOf(slot)));
}

std::vector<std::string> SaveSlotFiles::Slots() const
{
    std::vector<std::string> slots;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(Root, ec), end; !ec && it != end; it.increment(ec))
    {
        const std::string name = it->path().filename().string();
        for (const std::string_view extension : { kSlotExtension, kBackupExtension })
            if (name.ends_with(extension))
                if (std::string slot = name.substr(0, name.size() - extension.size()); IsSlotName(slot))
                    slots.push_back(std::move(slot));
    }
    std::ranges::sort(slots);
    const auto [first, last] = std::ranges::unique(slots);
    slots.erase(first, last);
    return slots;
}

std::filesystem::path SaveSlotFiles::PathOf(std::string_view slot) const
{
    return Root / (std::string(slot) + std::string(kSlotExtension));
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
