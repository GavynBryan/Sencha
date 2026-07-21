from pathlib import Path

path = Path("test/runtime/WorldPartitionRuntimeTests.cpp")
text = path.read_text()
old = '''    partition.Update(0.1, loader, runtimeWorld);
    runtimeWorld.FlushLifecycleRequests();
    tasks.PumpWork();
    tasks.DrainCompletions();
    runtimeWorld.FlushLifecycleRequests();
    (void)runtimeWorld.BeginResidencyProcessing();
    runtimeWorld.FinalizeResidencyProcessing();

    EXPECT_EQ(runtimeWorld.FindZone(kHallway), nullptr);
'''
new = '''    while (runtimeWorld.FindZone(kHallway) != nullptr)
    {
        partition.Update(0.1, loader, runtimeWorld);
        runtimeWorld.FlushLifecycleRequests();
        tasks.DrainCompletions();
        runtimeWorld.FlushLifecycleRequests();
        (void)runtimeWorld.BeginResidencyProcessing();
        runtimeWorld.FinalizeResidencyProcessing();
        ASSERT_LT(std::chrono::steady_clock::now(), deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_EQ(runtimeWorld.FindZone(kHallway), nullptr);
'''
if text.count(old) != 1:
    raise RuntimeError("threaded partition test tail changed unexpectedly")
path.write_text(text.replace(old, new, 1))

for cleanup in (
    Path("tools/temporary/fix_threaded_partition_test.py"),
    Path(".github/workflows/fix-threaded-partition-test.yml"),
):
    cleanup.unlink()
