#pragma once

class World;

//=============================================================================
// Camera registration
//
// World storage for the runtime camera vocabulary a game that drives cameras
// needs: CameraExclusion, which the render extractor reads. The authored
// CameraComponent is registered with the engine's scene vocabulary; this is
// the opt-in half. How a camera is placed is the game's system, not the
// engine's.
//=============================================================================
void RegisterCameraComponents(World& world);
