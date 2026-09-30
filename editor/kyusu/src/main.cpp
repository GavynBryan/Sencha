#include <app/Application.h>

#include "app/KyusuApp.h"
#include "project/ProjectArgs.h"

int main(int argc, char** argv)
{
    Application app(argc, argv);
    return app.Run<KyusuApp>(ResolveProjectPath(argc, argv));
}
