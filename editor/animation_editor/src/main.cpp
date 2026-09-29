#include "AnimationEditorApp.h"
#include "project/ProjectArgs.h"

#include <app/Application.h>

#include <string_view>

namespace
{
std::optional<std::string> Option(int argc, char** argv, std::string_view name)
{
    for (int i = 1; i + 1 < argc; ++i)
        if (argv[i] && std::string_view(argv[i]) == name && argv[i + 1])
            return std::string(argv[i + 1]);
    return std::nullopt;
}
}

int main(int argc, char** argv)
{
    Application app(argc, argv);
    return app.Run<AnimationEditorApp>(ResolveProjectPath(argc, argv),
        Option(argc, argv, "--mesh"), Option(argc, argv, "--clip"));
}
