from pathlib import Path

path = Path("test/runtime/WorldPartitionRuntimeTests.cpp")
text = path.read_text()

old = '''#include <core/json/JsonParser.h>
#include <jobs/AsyncTaskQueue.h>
#include <runtime/FrameDriver.h>
#include <runtime/RuntimeFrameLoop.h>
#include <world/registry/Registry.h>
#include <zone/AsyncZoneLoader.h>
#include <zone/WorldPartitionRuntime.h>
#include <zone/ZoneRuntime.h>
'''
new = '''#include <core/json/JsonParser.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/WorldComponentSchema.h>
#include <jobs/AsyncTaskQueue.h>
#include <runtime/FrameDriver.h>
#include <runtime/RuntimeFrameLoop.h>
#include <world/RuntimeWorld.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializationContext.h>
#include <zone/AsyncZoneLoader.h>
#include <zone/WorldPartitionRuntime.h>
#include <zone/ZoneLoadPackage.h>
'''
if text.count(old) != 1:
    raise RuntimeError("WorldPartitionRuntimeTests include block changed")
text = text.replace(old, new, 1)

marker = '''WorldPartitionManifest FixtureManifest()
{
    const auto json = JsonParse(kFixtureJson);
    EXPECT_TRUE(json.has_value());
    std::string error;
    const auto manifest = ReadWorldPartitionManifest(*json, &error);
    EXPECT_TRUE(manifest.has_value()) << error;
    return *manifest;
}

'''
helper = marker + '''WorldComponentSchema MakeStreamingSchema()
{
    WorldComponentSchema schema;
    schema.Seal();
    return schema;
}

void FinishRuntimeDrain(RuntimeWorld& world)
{
    world.FlushLifecycleRequests();
    (void)world.BeginResidencyProcessing();
    world.FinalizeResidencyProcessing();
}

'''
if text.count(marker) != 1:
    raise RuntimeError("FixtureManifest marker changed")
text = text.replace(marker, helper, 1)

old = '''    WorldPartitionRuntimeTest()
        : Tasks(0)
        , Loader(Tasks, Zones, Runtime)
        , Partition(MakeRecipe(), WorldPartitionStreamingConfig{})
    {
    }
'''
new = '''    WorldPartitionRuntimeTest()
        : Tasks(0)
        , Schema(MakeStreamingSchema())
        , Zones(Schema)
        , SceneContext(Logging)
        , Loader(Tasks, Zones, Schema, Serializers, SceneContext, Runtime)
        , Partition(MakeRecipe(), WorldPartitionStreamingConfig{})
    {
    }
'''
if text.count(old) != 1:
    raise RuntimeError("fixture constructor changed")
text = text.replace(old, new, 1)

old = '''            ZoneLoadRecipe recipe;
            recipe.Build = [](Registry& registry) { registry.Entities.Create(); };
            recipe.Finalize = [this, zone](Registry&)
            { Finalized.push_back({ zone, Zones.GetParticipation(zone) }); };
            return recipe;
'''
new = '''            ZoneLoadRecipe recipe;
            recipe.Build = [](ZoneLoadPackage& package) { package.CreateEntity(); };
            recipe.Finalize = [this, zone](RuntimeWorld&, RuntimeZoneRecord& record)
            {
                Finalized.push_back({ zone, record.Participation });
                return true;
            };
            return recipe;
'''
if text.count(old) != 1:
    raise RuntimeError("fixture recipe changed")
text = text.replace(old, new, 1)

old = '''    void Step(double dt = 0.0)
    {
        Partition.Update(dt, Loader, Zones);
        Tasks.PumpWork();
        Tasks.DrainCompletions();
    }

    AsyncTaskQueue Tasks;
    ZoneRuntime Zones;
    RuntimeFrameLoop Runtime;
    AsyncZoneLoader Loader;
'''
new = '''    void Step(double dt = 0.0)
    {
        Partition.Update(dt, Loader, Zones);
        Zones.FlushLifecycleRequests();
        Tasks.PumpWork();
        Tasks.DrainCompletions();
        FinishRuntimeDrain(Zones);
    }

    LoggingProvider Logging;
    AsyncTaskQueue Tasks;
    WorldComponentSchema Schema;
    RuntimeWorld Zones;
    ComponentSerializerRegistry Serializers;
    SceneSerializationContext SceneContext;
    RuntimeFrameLoop Runtime;
    AsyncZoneLoader Loader;
'''
if text.count(old) != 1:
    raise RuntimeError("fixture fields/Step changed")
text = text.replace(old, new, 1)

# Standalone runtime harnesses recur throughout the file.
old = '''    AsyncTaskQueue tasks(1);
    ZoneRuntime zones;
    RuntimeFrameLoop runtime;
    AsyncZoneLoader loader(tasks, zones, runtime);
'''
new = '''    LoggingProvider logging;
    AsyncTaskQueue tasks(1);
    WorldComponentSchema schema = MakeStreamingSchema();
    RuntimeWorld zones(schema);
    ComponentSerializerRegistry serializers;
    SceneSerializationContext sceneContext(logging);
    RuntimeFrameLoop runtime;
    AsyncZoneLoader loader(tasks, zones, schema, serializers, sceneContext, runtime);
'''
if text.count(old) < 2:
    raise RuntimeError("expected threaded standalone harnesses")
text = text.replace(old, new)

old = '''    AsyncTaskQueue tasks(0);
    ZoneRuntime zones;
    RuntimeFrameLoop runtime;
    AsyncZoneLoader loader(tasks, zones, runtime);
'''
new = '''    LoggingProvider logging;
    AsyncTaskQueue tasks(0);
    WorldComponentSchema schema = MakeStreamingSchema();
    RuntimeWorld zones(schema);
    ComponentSerializerRegistry serializers;
    SceneSerializationContext sceneContext(logging);
    RuntimeFrameLoop runtime;
    AsyncZoneLoader loader(tasks, zones, schema, serializers, sceneContext, runtime);
'''
if text.count(old) < 4:
    raise RuntimeError("expected serial standalone harnesses")
text = text.replace(old, new)

# Package build callbacks replace detached registry creation.
text = text.replace(
    '[](Registry& registry) { registry.Entities.Create(); }',
    '[](ZoneLoadPackage& package) { package.CreateEntity(); }')
text = text.replace('[](Registry&) {}', '[](ZoneLoadPackage&) {}')
text = text.replace('[&](Registry&)', '[&](ZoneLoadPackage&)')

# Traversal finalize observes the hidden zone record, then permits publication.
old = '''            recipe.Finalize = [&run, &zones, &currentX, zone](Registry&)
            {
                run.Events.push_back("attach:" + ZoneIdToString(zone));
                run.AttachPositions.push_back({ zone, currentX });
                run.AttachParticipations.push_back(zones.GetParticipation(zone));
            };
'''
new = '''            recipe.Finalize = [&run, &currentX, zone](RuntimeWorld&, RuntimeZoneRecord& record)
            {
                run.Events.push_back("attach:" + ZoneIdToString(zone));
                run.AttachPositions.push_back({ zone, currentX });
                run.AttachParticipations.push_back(record.Participation);
                return true;
            };
'''
if text.count(old) != 1:
    raise RuntimeError("traversal finalize changed")
text = text.replace(old, new, 1)

# Loaded and participation observations now read RuntimeWorld records.
text = text.replace('.IsZoneLoaded(', '.IsZoneResident(')
text = text.replace('Zones.GetParticipation(', 'Zones.FindZone(')
text = text.replace('zones.GetParticipation(', 'zones.FindZone(')

# Complete the pointer-expression rewrites introduced above.
import re
text = re.sub(r'Zones\.FindZone\(([^\n;]+?)\)', r'Zones.FindZone(\1)->Participation', text)
text = re.sub(r'zones\.FindZone\(([^\n;]+?)\)', r'zones.FindZone(\1)->Participation', text)

# Policy calls enqueue participation; each simulated drain applies requests and
# completes final detach after the explicit residency visit.
text = text.replace(
    'Partition.Update(0.0, Loader, Zones);   // issues both loads, nothing pumped',
    'Partition.Update(0.0, Loader, Zones);   // issues both loads, nothing pumped')

# Insert drain completion after standalone task drains where state is observed.
text = text.replace(
    'tasks.DrainCompletions();   // destruction lands at the drain, as in a frame\n    EXPECT_FALSE(zones.IsZoneResident(kHallway));',
    'tasks.DrainCompletions();   // detach request lands at the drain\n    FinishRuntimeDrain(zones);\n    EXPECT_FALSE(zones.IsZoneResident(kHallway));')

# Scripted traversal: apply queued participation and finalize residency after
# every settle loop before observations.
old = '''        for (size_t i = 0; i < 3; ++i)
        {
'''
new = '''        FinishRuntimeDrain(zones);

        for (size_t i = 0; i < 3; ++i)
        {
'''
if text.count(old) != 1:
    raise RuntimeError("scripted traversal observation loop changed")
text = text.replace(old, new, 1)

# Full-tick and neighbor loops need the same explicit lifecycle drain.
text = text.replace(
    'partition.Update(1.0 / 60.0, loader, zones);\n                    });',
    'partition.Update(1.0 / 60.0, loader, zones);\n                        FinishRuntimeDrain(zones);\n                    });')
text = text.replace(
    'tasks.DrainCompletions();\n    }\n\n    ASSERT_TRUE(zones.IsZoneResident(kHallway));',
    'tasks.DrainCompletions();\n        FinishRuntimeDrain(zones);\n    }\n\n    ASSERT_TRUE(zones.IsZoneResident(kHallway));')

# Update the direct record access cases that span declarations.
text = text.replace(
    'const ZoneParticipation participation = Zones.FindZone(kHub)->Participation;',
    'const ZoneParticipation participation = Zones.FindZone(kHub)->Participation;')

# Sanity: no test may retain the old runtime or registry callback vocabulary.
for forbidden in (
    'ZoneRuntime zones',
    'ZoneRuntime Zones',
    'Registry& registry',
    'Registry&)',
    'GetParticipation(',
    'IsZoneLoaded(',
    'AsyncZoneLoader loader(tasks, zones, runtime)',
    'Loader(Tasks, Zones, Runtime)',
):
    if forbidden in text:
        raise RuntimeError(f"legacy world-partition test vocabulary remains: {forbidden}")

path.write_text(text)

for cleanup in (
    Path("tools/temporary/port_world_partition_runtime_tests.py"),
    Path(".github/workflows/port-world-partition-runtime-tests.yml"),
):
    cleanup.unlink()
