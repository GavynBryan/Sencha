from pathlib import Path

path = Path("test/runtime/CaptionSystemTests.cpp")
text = path.read_text()
marker = "// -- Scene serialization"
if text.count(marker) != 1:
    raise RuntimeError("caption serialization boundary changed")
runtime, serialization = text.split(marker, 1)

include = "#include <core/serialization/JsonArchive.h>\n"
replacement = include + "#include <ecs/StoragePartitionSet.h>\n#include <ecs/World.h>\n"
if runtime.count(include) != 1:
    raise RuntimeError("caption include anchor changed")
runtime = runtime.replace(include, replacement, 1)

old = '''    void SetupRegistry(Registry& registry, AudioClipCache* cache,
                       AudioService* audio, CaptionRuntime* captions)
    {
        registry.Components.AddResource<AudioSourceRuntime>(cache, audio, captions);
        registry.Components.RegisterComponent<AudioSourceComponent>();
        registry.Components.RegisterComponent<AudioCaptionComponent>();
    }
'''
new = '''    StoragePartitionSet ActivePartitions()
    {
        StoragePartitionSet partitions;
        partitions.Add(StoragePartitionId::Default());
        return partitions;
    }

    void SetupWorld(World& world, AudioClipCache* cache,
                    AudioService* audio, CaptionRuntime* captions)
    {
        world.AddResource<AudioSourceRuntime>(cache, audio, captions);
        world.RegisterComponent<AudioSourceComponent>();
        world.RegisterComponent<AudioCaptionComponent>();
    }
'''
if runtime.count(old) != 1:
    raise RuntimeError("SetupRegistry helper changed")
runtime = runtime.replace(old, new, 1)

runtime = runtime.replace("Registry registry;", "World world;")
runtime = runtime.replace("SetupRegistry(registry,", "SetupWorld(world,")
runtime = runtime.replace("registry.Entities.Create()", "world.CreateEntity()")
runtime = runtime.replace("registry.Components.", "world.")

runtime = runtime.replace(
    "std::vector<Registry*> active{ &registry };",
    "const StoragePartitionSet active = ActivePartitions();")
runtime = runtime.replace(
    "std::vector<Registry*> dormant{};",
    "const StoragePartitionSet dormant;")

runtime = runtime.replace(
    "audioSystem.Update(&audio, active);",
    "audioSystem.Update(&audio, world, active);")
runtime = runtime.replace(
    "audioSystem.Update(&audio, dormant);",
    "audioSystem.Update(&audio, world, dormant);")
runtime = runtime.replace(
    "audioSystem.Update(nullptr, active);",
    "audioSystem.Update(nullptr, world, active);")
runtime = runtime.replace(
    "captionSystem.Update(&captions, &audio, active, 0.016f);",
    "captionSystem.Update(&captions, &audio, world, active, 0.016f);")
runtime = runtime.replace(
    "captionSystem.Update(&captions, &audio, dormant, 0.016f);",
    "captionSystem.Update(&captions, &audio, world, dormant, 0.016f);")
runtime = runtime.replace(
    "captionSystem.Update(&captions, nullptr, active, 0.016f);",
    "captionSystem.Update(&captions, nullptr, world, active, 0.016f);")

for forbidden in (
    "SetupRegistry(",
    "Registry registry;",
    "std::vector<Registry*>",
    "audioSystem.Update(&audio, active)",
    "captionSystem.Update(&captions, &audio, active",
    "registry.Components",
    "registry.Entities",
):
    if forbidden in runtime:
        raise RuntimeError(f"legacy caption runtime pattern remains: {forbidden}")

path.write_text(runtime + marker + serialization)

for cleanup in (
    Path("tools/temporary/port_caption_runtime_tests.py"),
    Path(".github/workflows/port-caption-runtime-tests.yml"),
):
    cleanup.unlink()
