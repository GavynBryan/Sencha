#pragma once

//=============================================================================
// IService
//
// The boundary marker for the ServiceHost tier. Deriving from IService is the
// declaration that a type is an installed *service*: a capability the engine
// HAS, owned by ServiceHost, constructed in dependency order, destroyed in
// reverse, and resolved by concrete type at wiring time.
//
// The contract every service keeps:
//   - It takes its dependencies as explicit constructor parameters and never
//     resolves siblings back through the host. Reading a service's constructor
//     tells you everything it depends on.
//   - It tolerates reverse-construction-order teardown: it may use siblings
//     constructed before it, never ones constructed after.
//
// What is NOT a service -- do not derive from IService:
//   - Foundation: substrate every tier needs in order to construct at all,
//     e.g. LoggingProvider. Owned at the top, injected by reference.
//   - Machinery: the engine's own moving parts whose lifetime must bracket the
//     whole service set, or that the Engine drives directly each frame -- the
//     schedule, zones, frame loop/driver, timing, the worker lanes. The Engine
//     names these as members; they are not resolved by type.
//
// Mechanically the base also gives ServiceHost type-erased ownership
// (unique_ptr<IService>) and the static_assert guardrails on its templates.
//=============================================================================
class IService
{
public:
	virtual ~IService() = default;
};
