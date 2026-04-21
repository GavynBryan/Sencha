#include "EditorViewportCameraSystem.h"

#include "../viewport/EditorViewport.h"
#include "../viewport/FourWayViewportLayout.h"

#include <SDL3/SDL_scancode.h>

EditorViewportCameraSystem::EditorViewportCameraSystem(FourWayViewportLayout& layout)
    : Layout(layout)
{
}

void EditorViewportCameraSystem::FrameUpdate(FrameUpdateContext& ctx)
{
    EditorViewport* viewport = Layout.GetActiveViewport();
    if (viewport == nullptr || !viewport->WantsFlyCameraInput)
        return;

    Vec3d move;
    if (ctx.Input.IsKeyDown(SDL_SCANCODE_W))
        move += Vec3d::Forward();
    if (ctx.Input.IsKeyDown(SDL_SCANCODE_S))
        move += Vec3d::Backward();
    if (ctx.Input.IsKeyDown(SDL_SCANCODE_D))
        move += Vec3d::Right();
    if (ctx.Input.IsKeyDown(SDL_SCANCODE_A))
        move += Vec3d::Left();
    if (ctx.Input.IsKeyDown(SDL_SCANCODE_E))
        move += Vec3d::Up();
    if (ctx.Input.IsKeyDown(SDL_SCANCODE_Q))
        move += Vec3d::Down();

    viewport->Camera.ApplyPerspectiveMove(move, static_cast<float>(ctx.WallDeltaSeconds));
}
