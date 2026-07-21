from pathlib import Path

path = Path("test/runtime/AssetPreloadTests.cpp")
text = path.read_text()

old = '''#include <runtime/RuntimeFrameLoop.h>
#include <world/registry/Registry.h>
#include <zone/AsyncZoneLoader.h>
#include <zone/ZoneRuntime.h>
'''
new = '''#include <ecs/WorldComponentSchema.h>
#include <runtime/RuntimeFrameLoop.h>
#include <world/RuntimeWorld.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializationContext.h>
#include <zone/AsyncZoneLoader.h>
#include <zone/ZoneLoadPackage.h>
'''
if text.count(old) != 1:
    raise RuntimeError("AssetPreloadTests include block changed unexpectedly")
text = text.replace(old, new, 1)

old = '''    struct ZoneHarness : PreloadHarness
    {
        ZoneHarness()
            : Loader(Tasks, Zones, Runtime)
        {
        }

        ZoneRuntime Zones;
        RuntimeFrameLoop Runtime;
        AsyncZoneLoader Loader;
    };
'''
new = '''    struct ZoneHarness : PreloadHarness
    {
        static WorldComponentSchema MakeSchema()
        {
            WorldComponentSchema schema;
            schema.Seal();
            return schema;
        }

        ZoneHarness()
            : Schema(MakeSchema())
            , World(Schema)
            , SceneContext(Logging, &Assets)
            , Loader(Tasks, World, Schema, Serializers, SceneContext, Runtime)
        {
        }

        WorldComponentSchema Schema;
        RuntimeWorld World;
        ComponentSerializerRegistry Serializers;
        SceneSerializationContext SceneContext;
        RuntimeFrameLoop Runtime;
        AsyncZoneLoader Loader;
    };
'''
if text.count(old) != 1:
    raise RuntimeError("ZoneHarness block changed unexpectedly")
text = text.replace(old, new, 1)

replacements = [
    ('[](Registry&) {}', '[](ZoneLoadPackage&) {}'),
    ('[&](Registry&) { finalized = true; }',
     '[&](RuntimeWorld&, RuntimeZoneRecord&) { finalized = true; return true; }'),
    ('h.Zones.IsZoneLoaded(zone)', 'h.World.IsZoneResident(zone)'),
]
for old, new in replacements:
    if old not in text:
        raise RuntimeError(f"Expected AssetPreloadTests text missing: {old}")
    text = text.replace(old, new)

path.write_text(text)

for cleanup in (
    Path("tools/temporary/port_asset_preload_zone_harness.py"),
    Path(".github/workflows/port-asset-preload-zone-harness.yml"),
):
    cleanup.unlink()
