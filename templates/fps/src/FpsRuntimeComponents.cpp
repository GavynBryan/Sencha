#include "FpsGame.h"

#include "FpsComponents.h"

// Storage, a scene serializer, and a place in the replicated table follow from
// what each component's TypeSchema declares. The list itself lives in
// RegisterFpsComponents so cook fixtures can speak the same schema.
void FpsGame::OnRegisterComponents(ComponentRegistrar& registrar)
{
    RegisterFpsComponents(registrar);
}
