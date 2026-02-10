#pragma once

#include <system/ISystem.h>
#include <service/IService.h>
#include <math/Vec.h>
#include <cmath>

class ServiceProvider;
class Maze;
class Logger;
struct CameraState;

//=============================================================================
// InputState
//
// Service that holds the current frame's input snapshot. Written by the
// main loop after polling SDL events; read by PlayerSystem to drive
// movement and camera rotation.
//=============================================================================
struct InputState : public IService
{
	bool bForward  = false;
	bool bBackward = false;
	bool bLeft     = false;
	bool bRight    = false;
	bool bQuit     = false;

	float MouseDeltaX = 0.0f;
	float MouseDeltaY = 0.0f;

	float DeltaTime = 0.0f;
};

//=============================================================================
// PlayerSystem
//
// System that updates the first-person camera each frame based on input.
// Applies mouse look (yaw/pitch), keyboard movement (WASD), and AABB
// collision against maze walls.
//
// Collision uses a "try each axis independently" approach: attempt the
// full movement along X, then along Z, rejecting each axis if the
// player's bounding box would overlap a wall cell. This produces
// natural wall-sliding behavior.
//=============================================================================
class PlayerSystem : public ISystem
{
public:
	explicit PlayerSystem(const ServiceProvider& provider);

private:
	void Update() override;

	bool CollidesWithMaze(float x, float z) const;

	InputState& Input;
	CameraState& Camera;
	const Maze& MazeData;
	Logger& Log;

	static constexpr float MoveSpeed = 3.0f;
	static constexpr float MouseSensitivity = 0.002f;
	static constexpr float PlayerRadius = 0.2f;
	static constexpr float MaxPitch = 1.4f;
};
