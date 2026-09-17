#include <platform/UserPaths.h>

#include <cstdlib>

std::filesystem::path UserConfigDirectory()
{
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && xdg[0] != '\0')
        return xdg;
    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0')
        return std::filesystem::path(home) / ".config";
    return ".";
}
