#pragma once

#include <cstddef>

//=============================================================================
// PackedScalarGroup
//
// Trait marking a struct as Count contiguous same-typed scalars (Vec2/3/4,
// Quat). A tool that flattens a schema stops at such a member and treats it as
// one N-wide value read/written as Count contiguous Component at the member's
// offset, instead of recursing into Count separate leaves. Count == 0 (the
// default) means "not a group".
//
// Contiguity is the invariant: component i must live at offset + i*sizeof(Component),
// so a single base offset addresses the whole group. Specialize this beside the
// type's TypeSchema so the specialization always travels with the type.
//=============================================================================
template <typename T>
struct PackedScalarGroup
{
    static constexpr std::size_t Count = 0;
};
