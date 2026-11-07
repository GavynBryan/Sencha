#version 450

// Which 8x8 tiles of the frame the outline composite has work in. The first pass
// marks a tile holding any mask texel; the second grows that by how far the widest
// style reaches, in tiles, so the composite leaves every other pixel after one
// fetch. Mirrored by OutlineTilePush in OutlineTilePass.cpp.

layout(location = 0) out vec4 outTile;

layout(set = 0, binding = 0) uniform sampler2D uTextures[1024];

layout(constant_id = 0) const bool kGrow = false;

layout(push_constant) uniform OutlineTilePC {
    uint sourceIndex;
    int reachTiles;
} pc;

const int kTileSize = 8;

void main()
{
    ivec2 tile = ivec2(gl_FragCoord.xy);
    ivec2 last = textureSize(uTextures[pc.sourceIndex], 0) - ivec2(1);
    if (!kGrow)
    {
        // Four texels a fetch: each gather lands on the corner its 2x2 block shares.
        // Past the edge the clamp repeats the last texel, which this tile holds anyway.
        vec2 size = vec2(last + ivec2(1));
        vec2 origin = vec2(tile * kTileSize);
        for (int y = 1; y < kTileSize; y += 2)
            for (int x = 1; x < kTileSize; x += 2)
                if (any(notEqual(textureGather(uTextures[pc.sourceIndex], (origin + vec2(x, y)) / size, 0), vec4(0.0))))
                {
                    outTile = vec4(1.0);
                    return;
                }
        outTile = vec4(0.0);
        return;
    }
    for (int dy = -pc.reachTiles; dy <= pc.reachTiles; ++dy)
        for (int dx = -pc.reachTiles; dx <= pc.reachTiles; ++dx)
            if (texelFetch(uTextures[pc.sourceIndex], clamp(tile + ivec2(dx, dy), ivec2(0), last), 0).r != 0.0)
            {
                outTile = vec4(1.0);
                return;
            }
    outTile = vec4(0.0);
}
