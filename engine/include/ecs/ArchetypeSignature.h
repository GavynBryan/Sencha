#pragma once

#include <ecs/ComponentId.h>

#include <bitset>
#include <cstddef>

// ArchetypeSignature: a fixed-size bitset over ComponentIds.
// Two entities share an archetype iff their signatures are equal.
// Bit N is set iff the entity has the component with ComponentId N.
// Tag components occupy a signature bit but produce no per-entity column.
using ArchetypeSignature = std::bitset<MaxComponents>;

inline bool SignatureMatches(
    const ArchetypeSignature& entitySig,
    const ArchetypeSignature& required,
    const ArchetypeSignature& excluded)
{
    return (entitySig & required) == required &&
           (entitySig & excluded).none();
}
