from pathlib import Path

cpp = Path("engine/src/zone/WorldPartitionRuntime.cpp")
text = cpp.read_text()

text = text.replace(
    '#include <zone/WorldPartitionRuntime.h>\n',
    '#include <zone/WorldPartitionRuntime.h>\n\n#include <world/RuntimeWorld.h>\n',
    1)

old = '''void WorldPartitionRuntime::Update(double deltaSeconds, AsyncZoneLoader& loader,
                                    ZoneRuntime& zones)
'''
new = '''void WorldPartitionRuntime::Update(double deltaSeconds, AsyncZoneLoader& loader,
                                    RuntimeWorld& world)
'''
if text.count(old) != 1:
    raise RuntimeError("WorldPartitionRuntime::Update signature changed unexpectedly")
text = text.replace(old, new, 1)

text = text.replace('zones.IsZoneLoaded(', 'world.IsZoneResident(')

old = '''        if (!SameParticipation(zones.GetParticipation(record.Zone), record.Desired))
            zones.SetParticipation(record.Zone, record.Desired);
'''
new = '''        const RuntimeZoneRecord* resident = world.FindZone(record.Zone);
        if (resident != nullptr
            && !SameParticipation(resident->Participation, record.Desired))
        {
            (void)world.RequestParticipation(record.Zone, record.Desired);
        }
'''
if text.count(old) != 1:
    raise RuntimeError("demand participation update changed unexpectedly")
text = text.replace(old, new, 1)

old = '''        if (zones.GetParticipation(zone).Any())
            zones.SetParticipation(zone, ZoneParticipation{});
'''
new = '''        const RuntimeZoneRecord* resident = world.FindZone(zone);
        if (resident != nullptr && resident->Participation.Any())
            (void)world.RequestParticipation(zone, ZoneParticipation{});
'''
if text.count(old) != 1:
    raise RuntimeError("linger participation update changed unexpectedly")
text = text.replace(old, new, 1)

if 'zones.' in text or 'ZoneRuntime& zones' in text:
    raise RuntimeError("legacy ZoneRuntime references remain in policy update")

cpp.write_text(text)

for path in (
    Path("tools/temporary/port_partition_policy_runtime_world.py"),
    Path(".github/workflows/port-partition-policy-runtime-world.yml"),
):
    path.unlink()
