#include "project/MaterialLibraryActions.h"

#include <SDL3/SDL_keycode.h>

ActionDefinition MaterialExplorerAction()
{
    return { .Id = "explorer.materials", .Label = "Materials", .Category = "Explore", .Icon = IconId::Folder,
             .DefaultChords = { { SDLK_M, {} } } };
}

ActionDefinition MaterialRescanAction()
{
    return { .Id = "materials.rescan", .Label = "Rescan Materials", .Category = "Material", .Icon = IconId::Refresh };
}
