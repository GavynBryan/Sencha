#version 450
#extension GL_GOOGLE_include_directive : require

#include "bloom.glsli"

void main()
{
    vec4 scene = pc.sceneIndex != 0xFFFFFFFFu ? texture(uTextures[pc.sceneIndex], vUv) : vec4(0.0);
    outColor = vec4(scene.rgb + texture(uTextures[pc.sourceIndex], vUv).rgb * pc.intensity, scene.a);
}
