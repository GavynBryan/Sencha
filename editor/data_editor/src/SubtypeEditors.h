#pragma once

class SubtypeEditorRegistry;

// Wires the purpose-built subtype editors this build ships with.
//
// The composition root for authoring surfaces: adding one means a factory call
// here plus files in that feature's own directory. Registration is explicit
// rather than each editor announcing itself from a static initializer, because
// what a build contains should be readable in one place and not depend on
// whether the linker kept an object nothing references.
void RegisterBuiltInSubtypeEditors(SubtypeEditorRegistry& registry);
