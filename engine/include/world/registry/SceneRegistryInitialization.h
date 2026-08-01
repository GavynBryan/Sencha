#pragma once

class AudioClipCache;
class AudioService;
class CaptionRuntime;
class MaterialSetCache;
struct Registry;
class StaticMeshCache;
class TextureCache;

// Registers the scene component manifest and the resources those components
// resolve their handles through, so a bare Registry can hold a loaded scene.
//
// This is the document/editor path: a registry built here is populated by the
// scene serializer. Streamed runtime content takes the other route, arriving as
// a ZoneLoadPackage imported into RuntimeWorld storage partitions.
//
// The caches are optional because a caller that only inspects structure does
// not need to resolve assets; components whose cache is null keep unresolved
// handles.
void InitializeSceneRegistry(
    Registry& registry,
    StaticMeshCache* meshes = nullptr,
    MaterialSetCache* materialSets = nullptr,
    AudioClipCache* audioClips = nullptr,
    AudioService* audio = nullptr,
    CaptionRuntime* captions = nullptr,
    TextureCache* textures = nullptr);
