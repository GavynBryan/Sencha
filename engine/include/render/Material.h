#pragma once

#include <math/Vec.h>
#include <render/TextureHandle.h>

// Backend-neutral material description for the default 3D path. Render backends
// translate this into descriptors, samplers, and pipelines.
struct MaterialData
{
    Vec4 BaseColorFactor = Vec4(1.0f, 1.0f, 1.0f, 1.0f);
    float MetallicFactor = 0.0f;
    float RoughnessFactor = 1.0f;

    TextureHandle BaseColorTexture;
    TextureHandle NormalTexture;
    TextureHandle MetallicRoughnessTexture;
};

