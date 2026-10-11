#include "document/DocumentSerialization.h"
#include "document/WorldDocument.h"
#include "operations/EditorOperations.h"
#include "operations/LevelQueries.h"
#include "operations/OperationScript.h"
#include "project/Project.h"

#include <core/logging/LoggingProvider.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    constexpr int kAnswersHadErrors = 1;
    constexpr int kCouldNotStart = 2;

    constexpr std::string_view kUsage =
        "usage: kyusu_batch [--project <file.senchaproj>] --level <file.sscene> "
        "[--script <file>]\n"
        "Runs one editor operation per line of the script (stdin without --script) and "
        "writes one line of JSON per answer to stdout.\n";

    struct Options
    {
        std::string Project;
        std::string Level;
        std::string Script;
    };

    bool ParseOptions(int argc, char** argv, Options& options)
    {
        for (int index = 1; index < argc; ++index)
        {
            const std::string_view flag = argv[index];
            if (index + 1 >= argc)
                return false;
            const std::string value = argv[++index];
            if (flag == "--project")
                options.Project = value;
            else if (flag == "--level")
                options.Level = value;
            else if (flag == "--script")
                options.Script = value;
            else
                return false;
        }
        return !options.Level.empty();
    }
}

int main(int argc, char** argv)
{
    Options options;
    if (!ParseOptions(argc, argv, options))
    {
        std::fputs(kUsage.data(), stderr);
        return kCouldNotStart;
    }

    RegisterDocumentSerializers();
    LoggingProvider logging;
    WorldDocument world(logging);

    if (!options.Project.empty())
    {
        ProjectDescriptor project;
        std::string error;
        if (!ProjectDescriptor::Load(options.Project, project, &error))
        {
            std::fprintf(stderr, "kyusu_batch: %s\n", error.c_str());
            return kCouldNotStart;
        }
        std::vector<std::filesystem::path> roots(project.ContentRoots.begin(),
                                                 project.ContentRoots.end());
        world.SetContentRoots(std::move(roots));
    }

    if (!world.Load(options.Level))
    {
        std::fprintf(stderr, "kyusu_batch: could not open '%s'\n", options.Level.c_str());
        return kCouldNotStart;
    }

    EditorOperations operations;
    std::vector<std::string> errors;
    if (!operations.Register("level", LevelQueryOperations(world), errors))
    {
        for (const std::string& error : errors)
            std::fprintf(stderr, "kyusu_batch: %s\n", error.c_str());
        return kCouldNotStart;
    }

    std::size_t failed = 0;
    if (options.Script.empty())
    {
        failed = RunOperationScript(std::cin, std::cout, operations);
    }
    else
    {
        std::ifstream script(options.Script);
        if (!script)
        {
            std::fprintf(stderr, "kyusu_batch: could not read '%s'\n", options.Script.c_str());
            return kCouldNotStart;
        }
        failed = RunOperationScript(script, std::cout, operations);
    }
    return failed == 0 ? 0 : kAnswersHadErrors;
}
