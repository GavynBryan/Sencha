#include "ModuleComponents.h"

#include "document/DocumentSerialization.h"

#include <app/Game.h>
#include <world/ComponentRegistrar.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <span>

ModuleComponents::ModuleComponents(Game* module)
{
    if (module == nullptr)
        return;
    // No World and no session here, so the registrar carries neither: a game
    // component that only replicates is registered and simply has nowhere to go.
    ComponentRegistrar registrar(nullptr, &EditorSceneSerializers(), nullptr);
    module->OnRegisterComponents(registrar);
    const std::span<const ComponentTypeId> added = registrar.AddedSerializers();
    Added.assign(added.begin(), added.end());
    SetEditorModuleVocabulary([module](World& world) { module->OnRegisterVocabulary(world); });
}

ModuleComponents::~ModuleComponents()
{
    // The vocabulary installer goes with the serializers: its target is code in
    // the module's image. Documents already built keep the names they were
    // given -- they are values in their own worlds.
    for (ComponentTypeId type : Added)
        (void)EditorSceneSerializers().Remove(type);
    SetEditorModuleVocabulary({});
}
