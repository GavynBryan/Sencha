from pathlib import Path

path = Path("test/runtime/CaptionSystemTests.cpp")
text = path.read_text()
old = '''    // World SFX: scene-authored closed caption.
    Registry registry;
    SetupRegistry(registry, &cache, &audio, &captions);
    EntityId door = registry.Entities.Create();
    registry.Components.AddComponent(door, AudioSourceComponent{
        .Clip = clip, .Bus = "Sfx", .Looping = true });
    registry.Components.AddComponent(door, WorldCC("cc.door.slam"));

    AudioSystem audioSystem;
    CaptionSystem captionSystem;
    std::vector<Registry*> active{ &registry };
    audioSystem.Update(&audio, active);
    captionSystem.Update(&captions, &audio, active, 0.016f);
'''
new = '''    // World SFX: scene-authored closed caption in the active audio partition.
    World world;
    SetupWorld(world, &cache, &audio, &captions);
    EntityId door = world.CreateEntity();
    world.AddComponent(door, AudioSourceComponent{
        .Clip = clip, .Bus = "Sfx", .Looping = true });
    world.AddComponent(door, WorldCC("cc.door.slam"));

    AudioSystem audioSystem;
    CaptionSystem captionSystem;
    const StoragePartitionSet active = ActivePartitions();
    audioSystem.Update(&audio, world, active);
    captionSystem.Update(&captions, &audio, world, active, 0.016f);
'''
if text.count(old) != 1:
    raise RuntimeError("caption gate runtime block changed unexpectedly")
path.write_text(text.replace(old, new, 1))

for cleanup in (
    Path("tools/temporary/port_caption_gate_test.py"),
    Path(".github/workflows/port-caption-gate-test.yml"),
):
    cleanup.unlink()
