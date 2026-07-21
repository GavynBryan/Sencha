#pragma once

#include <ecs/EntityId.h>

//=============================================================================
// ActiveCameraService
//
// Simulation-scoped World resource that tracks which entity is the active
// camera. Under the legacy per-zone runtime it was installed once per Registry;
// the unified runtime owns one instance because every camera entity shares one
// generational EntityId universe.
//=============================================================================
class ActiveCameraService
{
public:
    void SetActive(EntityId entity) { Active = entity; }
    [[nodiscard]] EntityId GetActive() const { return Active; }
    [[nodiscard]] bool HasActive() const { return Active.IsValid(); }

private:
    EntityId Active;
};
