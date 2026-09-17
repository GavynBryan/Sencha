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
        argv[write++] = argv[read];
    }
    argc = write;
    return out;
}

std::string ResolveSettingsRoot(const HostArguments& arguments, std::string_view platformRoot)
{
    if (arguments.Settings.has_value())
        return *arguments.Settings == "none" ? std::string{} : *arguments.Settings;
    if (arguments.Headless)
        return {};
    return std::string(platformRoot);
}
