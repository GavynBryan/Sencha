#include "HorrorGame.h"

#include "HorrorComponents.h"

// Storage, a scene serializer, and a place in the replicated table follow from
// what each component's TypeSchema declares. The list itself lives in
// RegisterHorrorComponents so cook fixtures can speak the same schema.
void HorrorGame::OnRegisterComponents(ComponentRegistrar& registrar)
{
    RegisterHorrorComponents(registrar);
}
