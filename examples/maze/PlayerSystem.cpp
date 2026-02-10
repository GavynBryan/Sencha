#include "PlayerSystem.h"
#include "MazeRenderer.h"
#include "Maze.h"
#include <service/ServiceProvider.h>
#include <logging/Logger.h>
#include <algorithm>

PlayerSystem::PlayerSystem(const ServiceProvider& provider)
	: Input(provider.Get<InputState>())
	, Camera(provider.Get<CameraState>())
	, MazeData(provider.Get<Maze>())
	, Log(provider.GetLogger<PlayerSystem>())
{
}

void PlayerSystem::Update()
{
	float dt = Input.DeltaTime;

	// -- Mouse look ---------------------------------------------------------

	Camera.Yaw += Input.MouseDeltaX * MouseSensitivity;
	Camera.Pitch += Input.MouseDeltaY * MouseSensitivity;
	Camera.Pitch = std::clamp(Camera.Pitch, -MaxPitch, MaxPitch);

	// -- Movement -----------------------------------------------------------

	float cy = std::cos(Camera.Yaw);
	float sy = std::sin(Camera.Yaw);

	Vec3 forward(sy, 0.0f, -cy);
	Vec3 right(cy, 0.0f, sy);

	Vec3 move = Vec3::Zero();
	if (Input.bForward)  move += forward;
	if (Input.bBackward) move -= forward;
	if (Input.bLeft)     move -= right;
	if (Input.bRight)    move += right;

	if (move.SqrMagnitude() > 0.0f)
	{
		move = move.Normalized() * (MoveSpeed * dt);
	}

	// -- Collision (try each axis independently for wall sliding) -----------

	float newX = Camera.Position.X() + move.X();
	float newZ = Camera.Position.Z() + move.Z();

	if (!CollidesWithMaze(newX, Camera.Position.Z()))
	{
		Camera.Position.X() = newX;
	}

	if (!CollidesWithMaze(Camera.Position.X(), newZ))
	{
		Camera.Position.Z() = newZ;
	}
}

bool PlayerSystem::CollidesWithMaze(float x, float z) const
{
	// Player AABB: [x - radius, x + radius] x [z - radius, z + radius]
	float minX = x - PlayerRadius;
	float maxX = x + PlayerRadius;
	float minZ = z - PlayerRadius;
	float maxZ = z + PlayerRadius;

	// Check all grid cells the player AABB could overlap
	int colStart = static_cast<int>(std::floor(minX));
	int colEnd   = static_cast<int>(std::floor(maxX));
	int rowStart = static_cast<int>(std::floor(minZ));
	int rowEnd   = static_cast<int>(std::floor(maxZ));

	for (int row = rowStart; row <= rowEnd; ++row)
	{
		for (int col = colStart; col <= colEnd; ++col)
		{
			if (MazeData.IsWall(row, col))
			{
				// Wall AABB: [col, col+1] x [row, row+1]
				// Overlap test (already guaranteed by cell range, but explicit)
				if (maxX > static_cast<float>(col) &&
				    minX < static_cast<float>(col + 1) &&
				    maxZ > static_cast<float>(row) &&
				    minZ < static_cast<float>(row + 1))
				{
					return true;
				}
			}
		}
	}

	return false;
}
