#include <app/Application.h>

#include "DataEditorApp.h"

#include "project/ProjectArgs.h"

#include <optional>
#include <string>
#include <string_view>

namespace
{
    std::optional<std::string> ResolveAssetPath(int argc, char** argv)
    {
        for (int index = 1; index + 1 < argc; ++index)
        {
            if (std::string_view(argv[index]) == "--asset")
                return std::string(argv[index + 1]);
        }
        return std::nullopt;
    }
}

int main(int argc, char** argv)
{
    Application app(argc, argv);
    return app.Run<DataEditorApp>(ResolveProjectPath(argc, argv),
                                  ResolveAssetPath(argc, argv));
}
