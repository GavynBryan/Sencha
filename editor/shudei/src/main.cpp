#include <app/Application.h>

#include "MaterialEditorApp.h"

#include "project/ProjectArgs.h"

int main(int argc, char** argv)
{
    Application app(argc, argv);
    return app.Run<MaterialEditorApp>(ResolveProjectPath(argc, argv));
}
