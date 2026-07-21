from pathlib import Path

cpp = Path("engine/src/world/RuntimeWorld.cpp")
text = cpp.read_text()

old_ctor_tail = '''    // Partition zero is persistent and never receives a RuntimeZoneRecord.
    ZonesByPartition_.resize(1);
}
'''
new_ctor_tail = '''    // Partition zero is persistent and never receives a RuntimeZoneRecord.
    ZonesByPartition_.resize(1);
    FrameViewScratch_.Entities = &Entities_;
}
'''
if text.count(old_ctor_tail) != 1:
    raise RuntimeError("RuntimeWorld constructor tail changed unexpectedly")
text = text.replace(old_ctor_tail, new_ctor_tail, 1)

old_view = '''FrameZoneView RuntimeWorld::BuildFrameView()
{
    assert(!ResidencyProcessing_
           && "BuildFrameView before residency finalization");
    assert(!FrameViewLive_
           && "BuildFrameView called twice without EndFrameView");

    FrameZoneView view;
    view.Entities = &Entities_;

    view.Resident.Add(PersistentStoragePartition);
    view.Visible.Add(PersistentStoragePartition);
    view.Physics.Add(PersistentStoragePartition);
    view.Logic.Add(PersistentStoragePartition);
    view.Audio.Add(PersistentStoragePartition);

    // Slot order is stable and dense, giving every domain deterministic
    // partition iteration without sorting or allocation in query execution.
    for (std::size_t index = 1; index < ZonesByPartition_.size(); ++index)
    {
        const RuntimeZoneRecord* record = ZonesByPartition_[index].get();
        if (record == nullptr || record->State != RuntimeZoneLoadState::Resident)
            continue;

        view.Resident.Add(record->Partition);
        if (record->Participation.Visible)
            view.Visible.Add(record->Partition);
        if (record->Participation.Physics)
            view.Physics.Add(record->Partition);
        if (record->Participation.Logic)
            view.Logic.Add(record->Partition);
        if (record->Participation.Audio)
            view.Audio.Add(record->Partition);
    }

    FrameViewLive_ = true;
    return view;
}
'''
new_view = '''const FrameZoneView& RuntimeWorld::BuildFrameView()
{
    assert(!ResidencyProcessing_
           && "BuildFrameView before residency finalization");
    assert(!FrameViewLive_
           && "BuildFrameView called twice without EndFrameView");

    FrameViewScratch_.Visible.Clear();
    FrameViewScratch_.Physics.Clear();
    FrameViewScratch_.Logic.Clear();
    FrameViewScratch_.Audio.Clear();
    FrameViewScratch_.Resident.Clear();

    FrameViewScratch_.Resident.Add(PersistentStoragePartition);
    FrameViewScratch_.Visible.Add(PersistentStoragePartition);
    FrameViewScratch_.Physics.Add(PersistentStoragePartition);
    FrameViewScratch_.Logic.Add(PersistentStoragePartition);
    FrameViewScratch_.Audio.Add(PersistentStoragePartition);

    // Slot order is stable and dense, giving every domain deterministic
    // partition iteration without sorting or allocation in query execution.
    for (std::size_t index = 1; index < ZonesByPartition_.size(); ++index)
    {
        const RuntimeZoneRecord* record = ZonesByPartition_[index].get();
        if (record == nullptr || record->State != RuntimeZoneLoadState::Resident)
            continue;

        FrameViewScratch_.Resident.Add(record->Partition);
        if (record->Participation.Visible)
            FrameViewScratch_.Visible.Add(record->Partition);
        if (record->Participation.Physics)
            FrameViewScratch_.Physics.Add(record->Partition);
        if (record->Participation.Logic)
            FrameViewScratch_.Logic.Add(record->Partition);
        if (record->Participation.Audio)
            FrameViewScratch_.Audio.Add(record->Partition);
    }

    FrameViewLive_ = true;
    return FrameViewScratch_;
}
'''
if text.count(old_view) != 1:
    raise RuntimeError("BuildFrameView implementation changed unexpectedly")
cpp.write_text(text.replace(old_view, new_view, 1))

test = Path("test/runtime/UnifiedRuntimeWorldTests.cpp")
test_text = test.read_text()
for old, new in (
    ("const FrameZoneView view = runtime.BuildFrameView();",
     "const FrameZoneView& view = runtime.BuildFrameView();"),
    ("const FrameZoneView before = runtime.BuildFrameView();",
     "const FrameZoneView& before = runtime.BuildFrameView();"),
    ("const FrameZoneView after = runtime.BuildFrameView();",
     "const FrameZoneView& after = runtime.BuildFrameView();"),
):
    if test_text.count(old) != 1:
        raise RuntimeError(f"test view binding changed unexpectedly: {old}")
    test_text = test_text.replace(old, new, 1)
test.write_text(test_text)

for path in (
    Path("tools/temporary/reuse_runtime_frame_zone_view.py"),
    Path(".github/workflows/reuse-runtime-frame-zone-view.yml"),
):
    path.unlink()
