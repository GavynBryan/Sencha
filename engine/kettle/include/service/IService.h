#pragma once

//=============================================================================
// IService
//
// Minimal base class for services managed by ServiceHost. Exists for:
//   - Type-erased ownership (unique_ptr<IService>)
//   - static_assert guardrails in ServiceHost templates
//
// Services should NEVER depend on each other via the host. If a service
// needs another service, it takes it as an explicit constructor parameter.
//=============================================================================
class IService
{
public:
	virtual ~IService() = default;
};
