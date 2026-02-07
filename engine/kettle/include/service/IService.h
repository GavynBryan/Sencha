#pragma once

class GameServiceHost;

class IService
{
	friend class GameServiceHost;

public:
	virtual ~IService() = default;

protected:
	GameServiceHost& GetHost() const;

private:
	void SetHost(GameServiceHost* host) { Host = host; }

	GameServiceHost* Host = nullptr;
};
