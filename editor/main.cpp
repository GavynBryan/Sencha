#include <app/Application.h>

#include "app/EditorApp.h"

int main()
{
    Application app(0, nullptr);
    return app.Run<EditorApp>();
}
