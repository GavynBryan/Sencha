from pathlib import Path

cpp = Path("engine/src/world/RuntimeWorld.cpp")
text = cpp.read_text()
old = "RuntimeWorld::~RuntimeWorld() = default;\n"
new = '''RuntimeWorld::~RuntimeWorld()
{
    assert(!FrameViewLive_
           && "RuntimeWorld destroyed with a live FrameZoneView");
    assert(!ResidencyProcessing_
           && "RuntimeWorld destroyed during ZoneResidency processing");

    // Shutdown follows the same lifetime order as explicit detachment: entity
    // hooks run while simulation-scoped World resources and zone resources are
    // alive, then zone-scoped resources are destroyed. Persistent entities are
    // drained later by World teardown against the same World resources.
    for (std::size_t index = 1; index < ZonesByPartition_.size(); ++index)
    {
        if (ZonesByPartition_[index] == nullptr)
            continue;

        (void)Entities_.DestroyPartition(
            StoragePartitionId{ static_cast<std::uint16_t>(index) });
        ZonesByPartition_[index].reset();
    }
}
'''
if text.count(old) != 1:
    raise RuntimeError("RuntimeWorld destructor definition changed unexpectedly")
cpp.write_text(text.replace(old, new, 1))

for path in (
    Path("tools/temporary/finalize_runtime_world_shutdown.py"),
    Path(".github/workflows/finalize-runtime-world-shutdown.yml"),
):
    path.unlink()
