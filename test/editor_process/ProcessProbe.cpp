#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

std::string Utf8(const wchar_t* text)
{
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size == 0) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
    result.pop_back();
    return result;
}

int wmain(int argc, wchar_t** argv)
#else
int main(int argc, char** argv)
#endif
{
    if (argc < 2) return 1;
    std::ofstream output{std::filesystem::path(argv[1])};
    if (!output) return 2;
    output << argc - 2 << '\n';
    for (int i = 2; i < argc; ++i)
    {
#if defined(_WIN32)
        output << std::quoted(Utf8(argv[i])) << '\n';
#else
        output << std::quoted(std::string(argv[i])) << '\n';
#endif
    }
    output << std::quoted(std::filesystem::current_path().generic_string()) << '\n';
    return output.good() ? 0 : 3;
}
