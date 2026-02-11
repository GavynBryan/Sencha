#pragma once

class ISystem
{
	friend class SystemHost;

public:
	virtual ~ISystem() = default;

private:
	virtual void Init() {}
	virtual void Update() {}
	virtual void Shutdown() {}
};
