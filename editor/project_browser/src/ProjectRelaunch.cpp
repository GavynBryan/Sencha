#include "ProjectRelaunch.h"

ProcessCommand BuildProjectRelaunch(const std::filesystem::path& baseDir,
                                    std::string_view executable,
                                    const std::filesystem::path& projectPath)
{
    std::filesystem::path binary = baseDir / executable;
#if defined(_WIN32)
    binary += ".exe";
#endif
    return ProcessCommand{
        .Binary = binary.string(),
        .Args = { "--project", std::filesystem::absolute(projectPath).lexically_normal().string() },
    };
}
