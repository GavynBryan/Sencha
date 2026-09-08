#include "ArenaGame.h"

#include "ArenaComponents.h"

// Storage, a scene serializer, and a place in the replicated table follow from
// what each component's TypeSchema declares. The list itself lives in
// RegisterArenaComponents so cook fixtures can speak the same schema.
void ArenaGame::OnRegisterComponents(ComponentRegistrar& registrar)
{
    RegisterArenaComponents(registrar);
}
