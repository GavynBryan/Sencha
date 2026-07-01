#pragma once

struct FixedLogicContext;

//=============================================================================
// AttributeResolveSystem
//
// Recomputes every AttributeSet's Current from Base with active-effect modifiers
// folded in, once per fixed tick over the active logic registries. Separate from
// EffectLifetimeSystem on purpose: a just-activated effect changes Current before
// locomotion reads it, while short request tags (jump) are consumed before the
// lifetime pass ages or expires them.
//=============================================================================
class AttributeResolveSystem
{
public:
    void FixedLogic(FixedLogicContext& ctx);
};
