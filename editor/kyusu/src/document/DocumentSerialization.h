#pragma once

class ComponentSerializerRegistry;

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
