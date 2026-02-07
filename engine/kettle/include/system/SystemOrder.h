#pragma once

namespace SystemOrder
{
	constexpr int Clock = 0;
	constexpr int Timer = 10;
	constexpr int Input = 20;
	constexpr int Collision = 30;
	constexpr int CollisionResponse = 35;
	constexpr int World = 50;
	constexpr int Render = 100;  // Render last after all updates
}
