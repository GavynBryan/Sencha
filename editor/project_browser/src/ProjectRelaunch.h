#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

struct ProcessCommand
{
    std::string Binary;
    std::vector<std::string> Args;
};

// The command that opens `projectPath` in the application `executable` beside
// `baseDir`: a new process, because a project's content and module are the
// process's for its whole life.
[[nodiscard]] ProcessCommand BuildProjectRelaunch(const std::filesystem::path& baseDir,
                                                  std::string_view executable,
                                                  const std::filesystem::path& projectPath);
