#pragma once

#include <time/FrameTime.h>

class ISystem
{
	friend class SystemHost;

public:
	virtual ~ISystem() = default;

private:
	virtual void Init() {}
	virtual void Update(const FrameTime& time) {}
	virtual void Shutdown() {}
};
