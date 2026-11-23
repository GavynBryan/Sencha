#include <core/io/FileBytes.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <fstream>
#include <ios>
#include <string>
#include <system_error>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

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

bool ReadFilePrefix(const std::filesystem::path& path,
                    const std::function<std::size_t(std::span<const std::byte>)>& wanted,
                    std::vector<std::byte>& out)
{
    std::ifstream file;
    file.rdbuf()->pubsetbuf(nullptr, 0);
    file.open(path, std::ios::binary);
    if (!file.is_open())
        return false;
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size < 0)
        return false;
    file.seekg(0, std::ios::beg);

    out.clear();
    for (std::size_t want = std::min(wanted(out), static_cast<std::size_t>(size)); want > out.size();
         want = std::min(wanted(out), static_cast<std::size_t>(size)))
    {
        const std::size_t have = out.size();
        out.resize(want);
        if (!file.read(reinterpret_cast<char*>(out.data() + have), static_cast<std::streamsize>(want - have)))
            return false;
    }
    return true;
}

namespace
{
#if defined(_WIN32)
    int CreateNew(const std::filesystem::path& path)
    {
        return _wopen(path.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
    }
    bool WriteSome(int file, const std::byte* data, std::size_t size, std::size_t& written)
    {
        const int result = _write(file, data, static_cast<unsigned>(size > 0x40000000u ? 0x40000000u : size));
        written = result > 0 ? static_cast<std::size_t>(result) : 0;
        return result > 0;
    }
    bool SyncToDisk(int file) { return _commit(file) == 0; }
    bool Close(int file) { return _close(file) == 0; }
    void SyncDirectoryOf(const std::filesystem::path&) {}
    std::uint64_t ProcessNumber() { return static_cast<std::uint64_t>(_getpid()); }
#else
    int CreateNew(const std::filesystem::path& path)
    {
        return ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
    }
    bool WriteSome(int file, const std::byte* data, std::size_t size, std::size_t& written)
    {
        const ssize_t result = ::write(file, data, size);
        written = result > 0 ? static_cast<std::size_t>(result) : 0;
        return result > 0;
    }
    bool SyncToDisk(int file) { return ::fsync(file) == 0; }
    bool Close(int file) { return ::close(file) == 0; }
    void SyncDirectoryOf(const std::filesystem::path& path)
    {
        const std::filesystem::path directory = path.has_parent_path() ? path.parent_path() : ".";
        const int handle = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (handle < 0)
            return;
        (void)::fsync(handle);
        (void)::close(handle);
    }
    std::uint64_t ProcessNumber() { return static_cast<std::uint64_t>(::getpid()); }
#endif

    std::filesystem::path UniqueStagingPathFor(const std::filesystem::path& path)
    {
        static std::atomic<std::uint64_t> next{ 0 };
        std::filesystem::path staged = path;
        staged += "." + std::to_string(ProcessNumber()) + "-" + std::to_string(next.fetch_add(1)) + ".tmp";
        return staged;
    }

    bool WriteDurably(const std::filesystem::path& path, std::span<const std::byte> bytes)
    {
        const int file = CreateNew(path);
        if (file < 0)
            return false;
        bool ok = true;
        for (std::size_t done = 0, written = 0; ok && done < bytes.size(); done += written)
            ok = WriteSome(file, bytes.data() + done, bytes.size() - done, written);
        ok = ok && SyncToDisk(file);
        return Close(file) && ok;
    }
}

bool WriteFileBytes(const std::filesystem::path& path, std::span<const std::byte> bytes)
{
    const std::filesystem::path staged = UniqueStagingPathFor(path);
    std::error_code ec;
    if (WriteDurably(staged, bytes))
    {
        std::filesystem::rename(staged, path, ec);
        if (!ec)
        {
            SyncDirectoryOf(path);
            return true;
        }
    }
    std::filesystem::remove(staged, ec);
    return false;
}

bool WriteFileText(const std::filesystem::path& path, std::string_view text)
{
    return WriteFileBytes(path, std::as_bytes(std::span(text.data(), text.size())));
}
