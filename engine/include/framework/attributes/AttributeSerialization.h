#pragma once

//=============================================================================
// AttributeSet scene serialization
//
// Persists an AttributeSet by attribute *name* + base value, resolved through
// the AttributeRegistry on load. Current is derived (runtime), so it is not
// persisted — ResolveAttributes recomputes it. JSON-first; see the binary note
// in GameplayTagSerialization for the shared array-count limitation.
//=============================================================================

struct IWriteArchive;
struct IReadArchive;
struct AttributeSet;
class AttributeRegistry;

bool WriteAttributes(IWriteArchive& archive,
                     const AttributeSet& attributes,
                     const AttributeRegistry& registry);

bool ReadAttributes(IReadArchive& archive,
                    AttributeSet& attributes,
                    const AttributeRegistry& registry);
