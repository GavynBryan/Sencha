#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Where each save slot's bytes live: <root>/<slot>.ssav, and the last readable
// file it replaced beside it as <slot>.ssav.bak. Holds no state but the root, so a
// task may use a copy.
class SaveSlotFiles
{
public:
    explicit SaveSlotFiles(std::filesystem::path root);

    // Letters, digits, '-' and '_', at most 64 of them.
    [[nodiscard]] static bool IsSlotName(std::string_view slot);

    // Each replaces its file in one step.
    [[nodiscard]] bool Write(std::string_view slot, std::span<const std::byte> bytes) const;
    [[nodiscard]] bool WriteBackup(std::string_view slot, std::span<const std::byte> bytes) const;
    [[nodiscard]] std::optional<std::vector<std::byte>> Read(std::string_view slot) const;
    [[nodiscard]] std::optional<std::vector<std::byte>> ReadBackup(std::string_view slot) const;
    // `wanted` says how far to read, as ReadFilePrefix's does.
    [[nodiscard]] std::optional<std::vector<std::byte>>
    ReadPrefix(std::string_view slot, const std::function<std::size_t(std::span<const std::byte>)>& wanted) const;
    [[nodiscard]] std::optional<std::vector<std::byte>>
    ReadBackupPrefix(std::string_view slot, const std::function<std::size_t(std::span<const std::byte>)>& wanted) const;
    // The file and its backup; true when neither is left.
    [[nodiscard]] bool Remove(std::string_view slot) const;
    [[nodiscard]] bool RemoveBackup(std::string_view slot) const;

    // Every slot with a file or a backup, in name order.
    [[nodiscard]] std::vector<std::string> Slots() const;

    [[nodiscard]] std::filesystem::path PathOf(std::string_view slot) const;

private:
    std::filesystem::path Root;
};
