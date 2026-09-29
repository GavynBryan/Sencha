#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

// The file as a document last read or wrote it, hashed as it sits on disk. A later
// change is judged by content, so a touch or a copy of the same bytes is not one.
class FileBaseline
{
public:
    // Call right after reading or writing the file.
    void Record(const std::filesystem::path& file);
    void RecordAbsent();

    [[nodiscard]] bool FileChanged(const std::filesystem::path& file) const;

private:
    bool Exists = false;
    std::optional<std::filesystem::file_time_type> WriteTime;
    std::uint64_t ContentHash = 0;
};
