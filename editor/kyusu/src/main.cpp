#include <app/Application.h>

#include "app/EditorApp.h"
#include "project/ProjectArgs.h"

int main(int argc, char** argv)
{
    Application app(argc, argv);
    return app.Run<EditorApp>(ResolveProjectPath(argc, argv));
}
