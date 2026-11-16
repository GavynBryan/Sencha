#include "HostArguments.h"

#include <cstring>

HostArguments ParseHostArguments(int& argc, char** argv)
{
    HostArguments out;
    int write = 1;
    for (int read = 1; read < argc; ++read)
    {
        const char* arg = argv[read];
        const bool hasValue = read + 1 < argc;
        if (std::strcmp(arg, "--headless") == 0)
        {
            out.Headless = true;
            continue;
        }
        if (std::strcmp(arg, "--game") == 0 && hasValue)
        {
            out.GamePath = argv[++read];
            continue;
        }
        if (std::strcmp(arg, "--content-root") == 0 && hasValue)
        {
            out.ContentRoots.emplace_back(argv[++read]);
            continue;
        }
        if (std::strcmp(arg, "--settings") == 0 && hasValue)
        {
            out.Settings = argv[++read];
            continue;
        }
        if (std::strcmp(arg, "--saves") == 0 && hasValue)
        {
            out.Saves = argv[++read];
            continue;
        }
        argv[write++] = argv[read];
    }
    argc = write;
    return out;
}

namespace
{
    std::string ResolveUserRoot(const std::optional<std::string>& named, bool headless, std::string_view platformRoot)
    {
        if (named.has_value())
            return *named == "none" ? std::string{} : *named;
        if (headless)
            return {};
        return std::string(platformRoot);
    }
}

std::string ResolveSettingsRoot(const HostArguments& arguments, std::string_view platformRoot)
{
    return ResolveUserRoot(arguments.Settings, arguments.Headless, platformRoot);
}

std::string ResolveSavesRoot(const HostArguments& arguments, std::string_view platformRoot)
{
    return ResolveUserRoot(arguments.Saves, arguments.Headless, platformRoot);
}
