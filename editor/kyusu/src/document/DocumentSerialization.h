#pragma once

#include <functional>

class ComponentSerializerRegistry;
class World;

// The editor's component serializer set: the engine scene manifest plus the
// authoring-only components (brushes) that never reach a cooked scene, plus
// whatever the loaded game module registers.
//
// Owned by the editor application, not the engine -- the engine holds its own
// registry for the runtime, and the two vocabularies are deliberately
// different. Still reached through an accessor rather than threaded into each
// consumer: the panels, the visual renderer, and the entity recipe read it
// without holding a document, so giving them an explicit reference is a
// separate change to their construction.
[[nodiscard]] ComponentSerializerRegistry& EditorSceneSerializers();

// Populates EditorSceneSerializers(). Idempotent; call once at editor startup
// before any save or load.
void RegisterDocumentSerializers();

// The loaded game module's gameplay vocabulary -- its tags, attributes,
// abilities, and locomotion modes -- as something every authoring document can
// replay into its own World. Content names them as strings, so a document
// without them reads back a scene with the game's tags missing and offers a
// designer only the engine's.
//
// Held beside the serializer registry, and for the same reason: documents are
// created from several places (the focus document, each open zone, the cook's
// own), none of which holds the loaded module. Set it at module load and clear
// it before unmapping -- the callable's target lives in the module's image.
// Setting it does not reach documents that already exist.
void SetEditorModuleVocabulary(std::function<void(World&)> install);
void InstallEditorModuleVocabulary(World& world);
