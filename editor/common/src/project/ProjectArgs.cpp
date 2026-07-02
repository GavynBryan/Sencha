#include "project/ProjectArgs.h"

#include <cstdlib>
#include <cstring>

std::optional<std::string> ResolveProjectPath(int argc, char** argv)
{
    if (argv != nullptr)
    {
        for (int i = 1; i + 1 < argc; ++i)
        {
            if (argv[i] != nullptr && std::strcmp(argv[i], "--project") == 0 &&
                argv[i + 1] != nullptr && argv[i + 1][0] != '\0')
            {
                return std::string(argv[i + 1]);
            }
        }
    }

    if (const char* env = std::getenv("SENCHA_PROJECT"); env != nullptr && env[0] != '\0')
        return std::string(env);

    return std::nullopt;
}
