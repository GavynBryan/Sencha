#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
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

    [[nodiscard]] std::filesystem::path PathOf(std::string_view slot) const;

private:
    std::filesystem::path Root;
};
