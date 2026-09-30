#pragma once

class Engine;
class IRenderFeature;

// Removes a render feature before the state it borrows is destroyed. A refusal
// would leave it drawing through dangling references, so it is fatal.
void DetachRenderFeature(Engine& engine, IRenderFeature*& feature);

template <typename Feature>
void DetachRenderFeature(Engine& engine, Feature*& feature)
{
    IRenderFeature* base = feature;
    DetachRenderFeature(engine, base);
    feature = nullptr;
}
