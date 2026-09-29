#include "documents/FileBaseline.h"

#include <core/hash/ContentHash.h>

void FileBaseline::Record(const std::filesystem::path& file)
{
    Exists = HashFileContents(file.string(), ContentHash);
    std::error_code error;
    const auto time = std::filesystem::last_write_time(file, error);
    WriteTime = error ? std::nullopt : std::optional(time);
}

void FileBaseline::RecordAbsent()
{
    Exists = false;
    WriteTime.reset();
    ContentHash = 0;
}

bool FileBaseline::FileChanged(const std::filesystem::path& file) const
{
    std::error_code error;
    if (!std::filesystem::exists(file, error))
        return Exists;
    if (!Exists)
        return true;
    const auto time = std::filesystem::last_write_time(file, error);
    if (!error && WriteTime == time)
        return false;
    std::uint64_t hash = 0;
    return !HashFileContents(file.string(), hash) || hash != ContentHash;
}
