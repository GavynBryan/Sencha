#pragma once

#include <ecs/ComponentId.h>
#include <ecs/EntityId.h>

#include <cstdint>

class World;

// Runtime lifecycle dispatch for a dynamically-registered component.
//
// This is the runtime generalization of the compile-time ComponentTraits: a
// component whose ComponentId is only known at runtime (a script-defined
// component observed by a T `observer`) registers its transition callbacks here,
// keyed by ComponentId. The consolidated add/remove/destroy cores consult this
// table and fire synchronously at the committed transition, decoupled from
// payload size, so a zero-size tag is a valid trigger.
//
// The callee reads live committed state and defers its effects (it never
// synchronously mutates archetypes; the reaction-depth guard asserts if it
// tries). `component` points at the committed component bytes, or is null for a
// tag. `context` is the opaque owner installed at registration (the observer
// registry); World never inspects it.
struct TransitionDispatch
{
    void (*OnAdd)(World&, EntityId, ComponentId, const void* component, void* context) = nullptr;
    void (*OnRemove)(World&, EntityId, ComponentId, const void* component, void* context) = nullptr;

    // Fires when a deferred field mutation (set or delta) to an observable field
    // commits and actually changes the bytes. `fieldOffset`/`size` locate the
    // leaf field within the component; `oldBytes`/`newBytes` are that field's
    // value before and after (both live only across the synchronous call).
    void (*OnSet)(World&, EntityId, ComponentId, std::uint16_t fieldOffset,
                  const void* oldBytes, const void* newBytes, std::uint16_t size,
                  void* context) = nullptr;

    void* Context = nullptr;
};
