#include "ShojiApp.h"

#include "project/ProjectArgs.h"

#include <app/Application.h>

#include <cstring>
#include <optional>
#include <string>

namespace
{
    // --document <asset path>: the document to open at launch, e.g.
    // asset://ui/pause.rml. Optional; the library lists them all.
    std::optional<std::string> ResolveDocumentArg(int argc, char** argv)
    {
        for (int i = 1; i + 1 < argc; ++i)
            if (std::strcmp(argv[i], "--document") == 0)
                return std::string(argv[i + 1]);
        return std::nullopt;
    }
}

int main(int argc, char** argv)
{
    Application app(argc, argv);
    return app.Run<ShojiApp>(ResolveProjectPath(argc, argv), ResolveDocumentArg(argc, argv));
}
