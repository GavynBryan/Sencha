#pragma once

#include <ecs/EntityId.h>

//=============================================================================
// ActiveCameraService
//
// Registry-local resource that tracks which entity is the active camera. Kept in
// the neutral component/service catalog so gameplay camera follow, world setup,
// and render extraction can share it without depending on render headers.
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
