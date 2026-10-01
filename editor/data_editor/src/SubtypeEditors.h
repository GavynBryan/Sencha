#pragma once

class DataSubtypeAuthoringRegistry;

// Wires what the data subtypes this build ships with add to their schemas'
// properties.
//
// The composition root for subtype authoring: adding one means a factory call
// here plus files in that feature's own directory. Registration is explicit
// rather than each announcing itself from a static initializer, because what a
// build contains should be readable in one place and not depend on whether the
// linker kept an object nothing references.
void RegisterBuiltInSubtypeAuthoring(DataSubtypeAuthoringRegistry& registry);
