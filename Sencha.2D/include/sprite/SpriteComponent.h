#pragma once

#include <assets/texture/TextureHandle.h>

#include <cstdint>
#include <span>
#include <vector>

//=============================================================================
// SpriteComponent
//
// Pure visual data for a 2D entity. Transform ownership lives in
// TransformStore<Transform2f>; SpriteStore joins sprite data back to entities
// through SparseSet's parallel owner list.
//=============================================================================
struct SpriteComponent
{
    TextureHandle Texture;
    float         Width   = 16.0f;
    float         Height  = 16.0f;
    uint32_t      Color   = 0xFFFFFFFFu;
    int32_t       SortKey = 0;
};