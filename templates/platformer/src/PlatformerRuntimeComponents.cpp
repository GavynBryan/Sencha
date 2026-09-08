#include "PlatformerGame.h"

#include "PlatformerComponents.h"

// Storage, a scene serializer, and a place in the replicated table follow from
// what each component's TypeSchema declares. The list itself lives in
// RegisterPlatformerComponents so cook fixtures can speak the same schema.
void PlatformerGame::OnRegisterComponents(ComponentRegistrar& registrar)
{
    RegisterPlatformerComponents(registrar);
}
