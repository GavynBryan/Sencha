#include "TemplateGame.h"

#include "TemplateComponents.h"

// Storage, a scene serializer, and a place in the replicated table follow from
// what each component's TypeSchema declares. The list itself lives in
// RegisterTemplateComponents so cook fixtures can speak the same schema.
void TemplateGame::OnRegisterComponents(ComponentRegistrar& registrar)
{
    RegisterTemplateComponents(registrar);
}
