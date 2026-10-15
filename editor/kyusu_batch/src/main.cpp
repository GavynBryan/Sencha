#include "commands/CommandStack.h"
#include "document/DocumentSerialization.h"
#include "document/WorldDocument.h"
#include "operations/EditorOperations.h"
#include "operations/LevelOperations.h"
#include "operations/LevelTarget.h"
#include "operations/OperationReference.h"
#include "operations/OperationScript.h"
#include "operations/ViewOperations.h"
#include "operations/WorldFocus.h"
#include "operations/WorldQueries.h"
#include "project/Project.h"
#include "workspace/EditorWorkspace.h"

#include <core/logging/LoggingProvider.h>
#include <zone/WorldPartitionIds.h>
#include <zone/WorldPartitionManifest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    constexpr int kAnswersHadErrors = 1;
    constexpr int kCouldNotStart = 2;

    constexpr std::string_view kUsage =
        "usage: kyusu_batch [--project <file.senchaproj>] (--level <file.sscene> | "
        "--world <file.sworld> [--zone <id or name>]) [--script <file>]\n"
        "       kyusu_batch --reference\n"
        "Runs one editor operation per line of the script (stdin without --script) and "
        "writes one line of JSON per answer to stdout. --reference prints every operation "
        "as markdown instead.\n";

    struct Options
    {
        std::string Project;
        std::string Level;
        std::string World;
        std::string Zone;
        std::string Script;
        bool        Reference = false;
    };

    bool ParseOptions(int argc, char** argv, Options& options)
    {
        for (int index = 1; index < argc; ++index)
        {
            const std::string_view flag = argv[index];
            if (flag == "--reference")
            {
                options.Reference = true;
                continue;
            }
            if (index + 1 >= argc)
                return false;
            const std::string value = argv[++index];
            if (flag == "--project")
                options.Project = value;
            else if (flag == "--level")
                options.Level = value;
            else if (flag == "--world")
                options.World = value;
            else if (flag == "--zone")
                options.Zone = value;
            else if (flag == "--script")
                options.Script = value;
            else
                return false;
        }
        if (options.Reference)
            return options.Level.empty() && options.World.empty() && options.Script.empty();
        return options.Level.empty() != options.World.empty()
            && (options.Zone.empty() || !options.World.empty());
    }

    bool RegisterOperations(WorldDocument& world, LevelTarget& level, EditorOperations& operations)
    {
        std::vector<std::string> errors;
        if (operations.Register("level", LevelOperations(level, operations), errors)
            && operations.Register("world", WorldOperations(world), errors))
            return true;
        for (const std::string& error : errors)
            std::fprintf(stderr, "kyusu_batch: %s\n", error.c_str());
        return false;
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
    // Answers must not depend on, or disturb, anyone's view of a world.
    WorldDocument world(logging, WorldDocument::UserState::Ignored);

    if (options.Reference)
    {
        // A workspace, so the reference also lists what only a running editor answers.
        CommandStack commands;
        EditorWorkspace workspace(logging, commands);
        EditorOperations operations;
        std::vector<std::string> errors;
        if (!RegisterOperations(workspace.World, workspace.Level, operations)
            || !operations.Register("view", ViewOperations({ workspace.World, workspace.Layout, workspace.Selection, workspace.Commands }), errors))
            return kCouldNotStart;
        std::cout << OperationReference(operations.Catalog());
        return 0;
    }

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

    const std::string& opened = options.World.empty() ? options.Level : options.World;
    if (!(options.World.empty() ? world.Load(options.Level) : world.LoadWorld(options.World)))
    {
        std::fprintf(stderr, "kyusu_batch: could not open '%s'\n", opened.c_str());
        return kCouldNotStart;
    }
    if (!options.Zone.empty())
    {
        const std::optional<ZoneId> zone = ZoneNamed(world.Manifest(), options.Zone);
        if (!zone || !world.SetFocusZone(*zone))
        {
            std::fprintf(stderr, "kyusu_batch: the world has no zone '%s'\n", options.Zone.c_str());
            return kCouldNotStart;
        }
    }

    CommandStack commands;
    LevelTarget level(world, commands);
    EditorOperations operations;
    if (!RegisterOperations(world, level, operations))
        return kCouldNotStart;

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
