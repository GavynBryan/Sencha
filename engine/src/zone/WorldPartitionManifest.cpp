#include <zone/WorldPartitionManifest.h>

#include <core/json/JsonValue.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>

namespace
{

void SetError(std::string* error, std::string message)
{
    if (error)
        *error = std::move(message);
}

std::string HexEncode(uint64_t value)
{
    return std::format("{:016x}", value);
}

// Same strictness as the id FromString helpers: exactly 16 lowercase hex
// digits, nonzero. Used for the cooked content hash, which follows the same
// text form without being an identity.
std::optional<uint64_t> HexDecode(std::string_view text)
{
    if (text.size() != 16)
        return std::nullopt;

    uint64_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        return std::nullopt;

    if (value == 0 || HexEncode(value) != text)
        return std::nullopt;
    return value;
}

// Reads a required id field through the strict parser. `where` names the
// field for the error message ("zones[3].id").
template <typename Id, typename FromString>
bool ReadRequiredId(const JsonValue& object,
                    const char* key,
                    FromString fromString,
                    Id& out,
                    std::string* error,
                    const std::string& where)
{
    const JsonValue* value = object.Find(key);
    if (!value)
    {
        SetError(error, std::format("{} is missing", where));
        return false;
    }
    if (!value->IsString())
    {
        SetError(error, std::format("{} is malformed", where));
        return false;
    }
    const auto id = fromString(value->AsString());
    if (!id)
    {
        SetError(error, std::format("{} is malformed", where));
        return false;
    }
    out = *id;
    return true;
}

// Reads an optional id field: absent leaves `out` invalid, present must parse.
template <typename Id, typename FromString>
bool ReadOptionalId(const JsonValue& object,
                    const char* key,
                    FromString fromString,
                    Id& out,
                    std::string* error,
                    const std::string& where)
{
    const JsonValue* value = object.Find(key);
    if (!value)
        return true;
    if (!value->IsString())
    {
        SetError(error, std::format("{} is malformed", where));
        return false;
    }
    const auto id = fromString(value->AsString());
    if (!id)
    {
        SetError(error, std::format("{} is malformed", where));
        return false;
    }
    out = *id;
    return true;
}

bool ReadOptionalString(const JsonValue& object,
                        const char* key,
                        std::string& out,
                        std::string* error,
                        const std::string& where)
{
    const JsonValue* value = object.Find(key);
    if (!value)
        return true;
    if (!value->IsString())
    {
        SetError(error, std::format("{} must be a string", where));
        return false;
    }
    out = value->AsString();
    return true;
}

bool ReadOptionalBool(const JsonValue& object,
                      const char* key,
                      bool& out,
                      std::string* error,
                      const std::string& where)
{
    const JsonValue* value = object.Find(key);
    if (!value)
        return true;
    if (!value->IsBool())
    {
        SetError(error, std::format("{} must be a boolean", where));
        return false;
    }
    out = value->AsBool();
    return true;
}

bool ReadVec3(const JsonValue& value, Vec3d& out)
{
    if (!value.IsArray() || value.Size() != 3)
        return false;
    const auto& array = value.AsArray();
    for (int i = 0; i < 3; ++i)
    {
        if (!array[static_cast<size_t>(i)].IsNumber())
            return false;
        out[i] = static_cast<float>(array[static_cast<size_t>(i)].AsNumber());
    }
    return true;
}

bool ReadVec2(const JsonValue& value, Vec2d& out)
{
    if (!value.IsArray() || value.Size() != 2)
        return false;
    const auto& array = value.AsArray();
    if (!array[0].IsNumber() || !array[1].IsNumber())
        return false;
    out.X = static_cast<float>(array[0].AsNumber());
    out.Y = static_cast<float>(array[1].AsNumber());
    return true;
}

bool ReadBounds(const JsonValue& zone, const char* key, Aabb3d& out,
                std::string* error, size_t index)
{
    const JsonValue* value = zone.Find(key);
    if (!value)
    {
        SetError(error, std::format("zones[{}].{} is missing", index, key));
        return false;
    }
    const JsonValue* min = value->Find("min");
    const JsonValue* max = value->Find("max");
    if (!value->IsObject() || !min || !max || !ReadVec3(*min, out.Min) || !ReadVec3(*max, out.Max))
    {
        SetError(error, std::format("zones[{}].{} is malformed", index, key));
        return false;
    }
    return true;
}

bool ReadUint32(const JsonValue& object, const char* key, uint32_t& out)
{
    const JsonValue* value = object.Find(key);
    if (value == nullptr || !value->IsNumber())
        return false;
    const double number = value->AsNumber();
    if (!std::isfinite(number) || number != std::floor(number) || number < 0.0
        || number > std::numeric_limits<uint32_t>::max())
        return false;
    out = static_cast<uint32_t>(number);
    return true;
}

bool ReadDockEndpoint(const JsonValue& value, DockEndpoint& out)
{
    std::string ignored;
    const JsonValue* side = value.Find("side");
    const JsonValue* origin = value.Find("origin");
    const JsonValue* normal = value.Find("normal");
    const JsonValue* right = value.Find("right");
    const JsonValue* up = value.Find("up");
    const JsonValue* halfExtents = value.Find("half_extents");
    return value.IsObject()
        && ReadRequiredId(value, "id", DockIdFromString, out.Id, &ignored, "id")
        && ReadRequiredId(value, "owner_zone", ZoneIdFromString, out.OwnerZone,
                          &ignored, "owner_zone")
        && ReadRequiredId(value, "other_zone", ZoneIdFromString, out.OtherZone,
                          &ignored, "other_zone")
        && side != nullptr && side->IsString()
        && (side->AsString() == "a" || side->AsString() == "b")
        && (out.Side = side->AsString() == "a" ? DockSide::A : DockSide::B, true)
        && origin != nullptr && ReadVec3(*origin, out.Origin)
        && normal != nullptr && ReadVec3(*normal, out.Normal)
        && right != nullptr && ReadVec3(*right, out.Right)
        && up != nullptr && ReadVec3(*up, out.Up)
        && halfExtents != nullptr && ReadVec2(*halfExtents, out.HalfExtents)
        && ReadUint32(value, "directions", out.Directions);
}

bool ReadLinkEndpoint(const JsonValue& value, LinkEndpoint& out)
{
    std::string ignored;
    const JsonValue* side = value.Find("side");
    return value.IsObject()
        && ReadRequiredId(value, "id", LinkIdFromString, out.Id, &ignored, "id")
        && ReadRequiredId(value, "owner_zone", ZoneIdFromString, out.OwnerZone,
                          &ignored, "owner_zone")
        && ReadRequiredId(value, "other_zone", ZoneIdFromString, out.OtherZone,
                          &ignored, "other_zone")
        && side != nullptr && side->IsString()
        && (side->AsString() == "a" || side->AsString() == "b")
        && (out.Side = side->AsString() == "a" ? DockSide::A : DockSide::B, true)
        && ReadUint32(value, "kind", out.Kind)
        && ReadUint32(value, "directions", out.Directions);
}

std::optional<TransitionTopology> TopologyFromString(std::string_view text)
{
    if (text == "seam")
        return TransitionTopology::Seam;
    if (text == "doorway")
        return TransitionTopology::Doorway;
    if (text == "teleport")
        return TransitionTopology::Teleport;
    return std::nullopt;
}

JsonValue WriteVec3(const Vec3d& v)
{
    JsonValue::Array array;
    array.emplace_back(static_cast<double>(v.X));
    array.emplace_back(static_cast<double>(v.Y));
    array.emplace_back(static_cast<double>(v.Z));
    return JsonValue{ std::move(array) };
}

JsonValue WriteDockEndpoint(const DockEndpoint& endpoint)
{
    JsonValue::Array halfExtents;
    halfExtents.emplace_back(static_cast<double>(endpoint.HalfExtents.X));
    halfExtents.emplace_back(static_cast<double>(endpoint.HalfExtents.Y));
    JsonValue::Object value;
    value.emplace_back("id", JsonValue{ DockIdToString(endpoint.Id) });
    value.emplace_back("owner_zone", JsonValue{ ZoneIdToString(endpoint.OwnerZone) });
    value.emplace_back("other_zone", JsonValue{ ZoneIdToString(endpoint.OtherZone) });
    value.emplace_back("side", JsonValue{ endpoint.Side == DockSide::A ? "a" : "b" });
    value.emplace_back("origin", WriteVec3(endpoint.Origin));
    value.emplace_back("normal", WriteVec3(endpoint.Normal));
    value.emplace_back("right", WriteVec3(endpoint.Right));
    value.emplace_back("up", WriteVec3(endpoint.Up));
    value.emplace_back("half_extents", JsonValue{ std::move(halfExtents) });
    value.emplace_back("directions", JsonValue{ static_cast<double>(endpoint.Directions) });
    return JsonValue{ std::move(value) };
}

JsonValue WriteLinkEndpoint(const LinkEndpoint& endpoint)
{
    JsonValue::Object value;
    value.emplace_back("id", JsonValue{ LinkIdToString(endpoint.Id) });
    value.emplace_back("owner_zone", JsonValue{ ZoneIdToString(endpoint.OwnerZone) });
    value.emplace_back("other_zone", JsonValue{ ZoneIdToString(endpoint.OtherZone) });
    value.emplace_back("side", JsonValue{ endpoint.Side == DockSide::A ? "a" : "b" });
    value.emplace_back("kind", JsonValue{ static_cast<double>(endpoint.Kind) });
    value.emplace_back("directions", JsonValue{ static_cast<double>(endpoint.Directions) });
    return JsonValue{ std::move(value) };
}

} // namespace

std::optional<WorldPartitionManifest>
ReadWorldPartitionManifest(const JsonValue& root, std::string* error)
{
    if (!root.IsObject())
    {
        SetError(error, "world partition manifest root must be an object");
        return std::nullopt;
    }

    const JsonValue* version = root.Find("format_version");
    if (!version || !version->IsNumber())
    {
        SetError(error, "format_version is required");
        return std::nullopt;
    }
    const double formatVersion = version->AsNumber();
    if (formatVersion != 1.0 && formatVersion != 3.0 && formatVersion != 4.0)
    {
        SetError(error, std::format("unsupported format_version {}", formatVersion));
        return std::nullopt;
    }
    const bool legacy = formatVersion == 1.0;

    WorldPartitionManifest manifest;

    if (!ReadOptionalString(root, "name", manifest.Name, error, "name"))
        return std::nullopt;
    if (!ReadOptionalId(root, "start_zone", ZoneIdFromString, manifest.StartZone, error, "start_zone"))
        return std::nullopt;
    if (!ReadOptionalString(root, "world_scene", manifest.WorldSceneRef, error, "world_scene"))
        return std::nullopt;
    if (!ReadOptionalString(root, "cooked_world_scene", manifest.CookedWorldSceneRef, error,
                            "cooked_world_scene"))
        return std::nullopt;
    if (!ReadOptionalString(root, "cooked_world_collision", manifest.CookedWorldCollisionRef,
                            error, "cooked_world_collision"))
        return std::nullopt;
    if (const JsonValue* hash = root.Find("world_scene_content_hash"))
    {
        const auto decoded = hash->IsString() ? HexDecode(hash->AsString()) : std::nullopt;
        if (!decoded)
        {
            SetError(error, "world_scene_content_hash is malformed");
            return std::nullopt;
        }
        manifest.CookedWorldContentHash = *decoded;
    }

    const char* graphArrayKey = legacy ? "regions" : "graphs";
    if (const JsonValue* graphs = root.Find(graphArrayKey))
    {
        if (!graphs->IsArray())
        {
            SetError(error, std::format("{} must be an array", graphArrayKey));
            return std::nullopt;
        }
        for (size_t i = 0; i < graphs->Size(); ++i)
        {
            const JsonValue& entry = graphs->AsArray()[i];
            GraphRecord record;
            if (!ReadRequiredId(entry, "id", GraphIdFromString, record.Id, error,
                                std::format("{}[{}].id", graphArrayKey, i)))
                return std::nullopt;
            if (!ReadOptionalString(entry, "name", record.Name, error,
                                    std::format("{}[{}].name", graphArrayKey, i)))
                return std::nullopt;
            if (const JsonValue* streaming = entry.Find("streaming"))
            {
                if (!streaming->IsObject())
                {
                    SetError(error, std::format("{}[{}].streaming must be an object",
                                                graphArrayKey, i));
                    return std::nullopt;
                }
                const auto readInt = [&](const char* key, std::optional<int32_t>& out)
                {
                    const JsonValue* value = streaming->Find(key);
                    if (!value)
                        return true;
                    const double number = value->IsNumber()
                        ? value->AsNumber()
                        : std::numeric_limits<double>::quiet_NaN();
                    if (!std::isfinite(number) || number != std::floor(number)
                        || number < std::numeric_limits<int32_t>::min()
                        || number > std::numeric_limits<int32_t>::max())
                    {
                        SetError(error,
                                 std::format("{}[{}].streaming.{} must be a 32-bit integer",
                                             graphArrayKey, i, key));
                        return false;
                    }
                    out = static_cast<int32_t>(number);
                    return true;
                };
                if (!readInt("hop_count", record.Streaming.HopCount))
                    return std::nullopt;
                if (!readInt("resident_zone_cap", record.Streaming.ResidentZoneCap))
                    return std::nullopt;
                if (const JsonValue* radius = streaming->Find("radius"))
                {
                    if (!radius->IsNumber())
                    {
                        SetError(error,
                                 std::format("{}[{}].streaming.radius must be a number",
                                             graphArrayKey, i));
                        return std::nullopt;
                    }
                    record.Streaming.Radius = radius->AsNumber();
                }
            }
            manifest.Graphs.push_back(std::move(record));
        }
    }

    if (const JsonValue* zones = root.Find("zones"))
    {
        if (!zones->IsArray())
        {
            SetError(error, "zones must be an array");
            return std::nullopt;
        }
        for (size_t i = 0; i < zones->Size(); ++i)
        {
            const JsonValue& entry = zones->AsArray()[i];
            ZoneHeader header;
            if (!ReadRequiredId(entry, "id", ZoneIdFromString, header.Id, error,
                                std::format("zones[{}].id", i)))
                return std::nullopt;
            if (!ReadOptionalString(entry, "name", header.Name, error,
                                    std::format("zones[{}].name", i)))
                return std::nullopt;
            const char* graphKey = legacy ? "region" : "graph";
            if (!ReadOptionalId(entry, graphKey, GraphIdFromString, header.Graph, error,
                                std::format("zones[{}].{}", i, graphKey)))
                return std::nullopt;
            if (!ReadOptionalString(entry, "scene", header.SceneRef, error,
                                    std::format("zones[{}].scene", i)))
                return std::nullopt;
            if (!ReadBounds(entry, "bounds", header.Bounds, error, i))
                return std::nullopt;
            if (!ReadOptionalBool(entry, "bounds_overridden", header.BoundsOverridden,
                                  error, std::format("zones[{}].bounds_overridden", i)))
                return std::nullopt;
            if (!ReadOptionalString(entry, "cooked_scene", header.CookedSceneRef, error,
                                    std::format("zones[{}].cooked_scene", i)))
                return std::nullopt;
            if (!ReadOptionalString(entry, "cooked_collision", header.CookedCollisionRef, error,
                                    std::format("zones[{}].cooked_collision", i)))
                return std::nullopt;
            if (const JsonValue* hash = entry.Find("content_hash"))
            {
                const auto decoded = hash->IsString() ? HexDecode(hash->AsString()) : std::nullopt;
                if (!decoded)
                {
                    SetError(error, std::format("zones[{}].content_hash is malformed", i));
                    return std::nullopt;
                }
                header.CookedContentHash = *decoded;
            }
            if (const JsonValue* docks = entry.Find("dock_endpoints"))
            {
                if (!docks->IsArray())
                {
                    SetError(error, std::format("zones[{}].dock_endpoints must be an array", i));
                    return std::nullopt;
                }
                for (const JsonValue& endpointValue : docks->AsArray())
                {
                    DockEndpoint endpoint;
                    if (!ReadDockEndpoint(endpointValue, endpoint))
                    {
                        SetError(error, std::format("zones[{}].dock_endpoints is malformed", i));
                        return std::nullopt;
                    }
                    header.Docks.push_back(std::move(endpoint));
                }
            }
            if (const JsonValue* links = entry.Find("link_endpoints"))
            {
                if (!links->IsArray())
                {
                    SetError(error, std::format("zones[{}].link_endpoints must be an array", i));
                    return std::nullopt;
                }
                for (const JsonValue& endpointValue : links->AsArray())
                {
                    LinkEndpoint endpoint;
                    if (!ReadLinkEndpoint(endpointValue, endpoint))
                    {
                        SetError(error, std::format("zones[{}].link_endpoints is malformed", i));
                        return std::nullopt;
                    }
                    header.Links.push_back(std::move(endpoint));
                }
            }
            manifest.Zones.push_back(std::move(header));
        }
    }

    if (const JsonValue* transitions = root.Find("transitions"))
    {
        if (!transitions->IsArray())
        {
            SetError(error, "transitions must be an array");
            return std::nullopt;
        }
        for (size_t i = 0; i < transitions->Size(); ++i)
        {
            const JsonValue& entry = transitions->AsArray()[i];
            TransitionRecord record;
            if (!ReadRequiredId(entry, "id", TransitionIdFromString, record.Id, error,
                                std::format("transitions[{}].id", i)))
                return std::nullopt;
            if (!ReadRequiredId(entry, "from", ZoneIdFromString, record.From, error,
                                std::format("transitions[{}].from", i)))
                return std::nullopt;
            if (!ReadRequiredId(entry, "to", ZoneIdFromString, record.To, error,
                                std::format("transitions[{}].to", i)))
                return std::nullopt;
            if (!ReadOptionalString(entry, "name", record.Name, error,
                                    std::format("transitions[{}].name", i)))
                return std::nullopt;
            if (const JsonValue* topology = entry.Find("topology"))
            {
                const auto parsed =
                    topology->IsString() ? TopologyFromString(topology->AsString()) : std::nullopt;
                if (!parsed)
                {
                    SetError(error, std::format("transitions[{}].topology is unknown", i));
                    return std::nullopt;
                }
                record.Topology = *parsed;
            }
            if (!ReadOptionalBool(entry, "one_way", record.Flags.OneWay, error,
                                  std::format("transitions[{}].one_way", i)))
                return std::nullopt;
            manifest.Transitions.push_back(record);
        }
    }

    const auto readDockDebugMap = [&]() -> bool
    {
        const JsonValue* values = root.Find("dock_debug_map");
        if (values == nullptr)
            return true;
        if (!values->IsArray())
            return false;
        for (const JsonValue& value : values->AsArray())
        {
            DockDebugRecord record;
            std::string ignored;
            const JsonValue* entity = value.Find("authored_entity");
            const auto decoded = entity != nullptr && entity->IsString()
                ? HexDecode(entity->AsString()) : std::nullopt;
            if (!value.IsObject()
                || !ReadRequiredId(value, "id", DockIdFromString, record.Id,
                                   &ignored, "dock_debug_map.id")
                || !decoded)
                return false;
            record.AuthoredEntity = *decoded;
            manifest.DockDebugMap.push_back(record);
        }
        return true;
    };
    const auto readLinkDebugMap = [&]() -> bool
    {
        const JsonValue* values = root.Find("link_debug_map");
        if (values == nullptr)
            return true;
        if (!values->IsArray())
            return false;
        for (const JsonValue& value : values->AsArray())
        {
            LinkDebugRecord record;
            std::string ignored;
            const JsonValue* entity = value.Find("authored_entity");
            const auto decoded = entity != nullptr && entity->IsString()
                ? HexDecode(entity->AsString()) : std::nullopt;
            if (!value.IsObject()
                || !ReadRequiredId(value, "id", LinkIdFromString, record.Id,
                                   &ignored, "link_debug_map.id")
                || !decoded)
                return false;
            record.AuthoredEntity = *decoded;
            manifest.LinkDebugMap.push_back(record);
        }
        return true;
    };
    if (!readDockDebugMap())
    {
        SetError(error, "dock_debug_map is malformed");
        return std::nullopt;
    }
    if (!readLinkDebugMap())
    {
        SetError(error, "link_debug_map is malformed");
        return std::nullopt;
    }

    return manifest;
}

JsonValue WriteWorldPartitionManifest(const WorldPartitionManifest& manifest)
{
    JsonValue::Object root;
    root.emplace_back("format_version", JsonValue{ 4 });
    root.emplace_back("name", JsonValue{ manifest.Name });
    if (manifest.StartZone.IsValid())
        root.emplace_back("start_zone", JsonValue{ ZoneIdToString(manifest.StartZone) });
    if (!manifest.WorldSceneRef.empty())
        root.emplace_back("world_scene", JsonValue{ manifest.WorldSceneRef });
    if (!manifest.CookedWorldSceneRef.empty())
        root.emplace_back("cooked_world_scene", JsonValue{ manifest.CookedWorldSceneRef });
    if (!manifest.CookedWorldCollisionRef.empty())
        root.emplace_back("cooked_world_collision", JsonValue{ manifest.CookedWorldCollisionRef });
    if (manifest.CookedWorldContentHash != 0)
        root.emplace_back("world_scene_content_hash",
                          JsonValue{ HexEncode(manifest.CookedWorldContentHash) });

    JsonValue::Array graphs;
    for (const GraphRecord& record : manifest.Graphs)
    {
        JsonValue::Object entry;
        entry.emplace_back("id", JsonValue{ GraphIdToString(record.Id) });
        entry.emplace_back("name", JsonValue{ record.Name });
        const GraphStreamingConfig& streaming = record.Streaming;
        if (streaming.HopCount || streaming.Radius || streaming.ResidentZoneCap)
        {
            JsonValue::Object object;
            if (streaming.HopCount)
                object.emplace_back("hop_count", JsonValue{ *streaming.HopCount });
            if (streaming.Radius)
                object.emplace_back("radius", JsonValue{ *streaming.Radius });
            if (streaming.ResidentZoneCap)
                object.emplace_back("resident_zone_cap", JsonValue{ *streaming.ResidentZoneCap });
            entry.emplace_back("streaming", JsonValue{ std::move(object) });
        }
        graphs.emplace_back(JsonValue{ std::move(entry) });
    }
    root.emplace_back("graphs", JsonValue{ std::move(graphs) });

    JsonValue::Array zones;
    for (const ZoneHeader& header : manifest.Zones)
    {
        JsonValue::Object entry;
        entry.emplace_back("id", JsonValue{ ZoneIdToString(header.Id) });
        entry.emplace_back("name", JsonValue{ header.Name });
        if (header.Graph.IsValid())
            entry.emplace_back("graph", JsonValue{ GraphIdToString(header.Graph) });
        entry.emplace_back("scene", JsonValue{ header.SceneRef });

        JsonValue::Object bounds;
        bounds.emplace_back("min", WriteVec3(header.Bounds.Min));
        bounds.emplace_back("max", WriteVec3(header.Bounds.Max));
        entry.emplace_back("bounds", JsonValue{ std::move(bounds) });
        if (header.BoundsOverridden)
            entry.emplace_back("bounds_overridden", JsonValue{ true });

        if (!header.CookedSceneRef.empty())
            entry.emplace_back("cooked_scene", JsonValue{ header.CookedSceneRef });
        if (!header.CookedCollisionRef.empty())
            entry.emplace_back("cooked_collision", JsonValue{ header.CookedCollisionRef });
        if (header.CookedContentHash != 0)
            entry.emplace_back("content_hash", JsonValue{ HexEncode(header.CookedContentHash) });

        if (!header.Docks.empty())
        {
            JsonValue::Array endpoints;
            for (const DockEndpoint& endpoint : header.Docks)
                endpoints.emplace_back(WriteDockEndpoint(endpoint));
            entry.emplace_back("dock_endpoints", JsonValue{ std::move(endpoints) });
        }
        if (!header.Links.empty())
        {
            JsonValue::Array endpoints;
            for (const LinkEndpoint& endpoint : header.Links)
                endpoints.emplace_back(WriteLinkEndpoint(endpoint));
            entry.emplace_back("link_endpoints", JsonValue{ std::move(endpoints) });
        }

        zones.emplace_back(JsonValue{ std::move(entry) });
    }
    root.emplace_back("zones", JsonValue{ std::move(zones) });

    if (!manifest.DockDebugMap.empty())
    {
        std::vector<DockDebugRecord> sorted = manifest.DockDebugMap;
        std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b)
                  { return a.Id.Value < b.Id.Value; });
        JsonValue::Array values;
        for (const DockDebugRecord& record : sorted)
        {
            JsonValue::Object value;
            value.emplace_back("id", JsonValue{ DockIdToString(record.Id) });
            value.emplace_back("authored_entity", JsonValue{ HexEncode(record.AuthoredEntity) });
            values.emplace_back(JsonValue{ std::move(value) });
        }
        root.emplace_back("dock_debug_map", JsonValue{ std::move(values) });
    }
    if (!manifest.LinkDebugMap.empty())
    {
        std::vector<LinkDebugRecord> sorted = manifest.LinkDebugMap;
        std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b)
                  { return a.Id.Value < b.Id.Value; });
        JsonValue::Array values;
        for (const LinkDebugRecord& record : sorted)
        {
            JsonValue::Object value;
            value.emplace_back("id", JsonValue{ LinkIdToString(record.Id) });
            value.emplace_back("authored_entity", JsonValue{ HexEncode(record.AuthoredEntity) });
            values.emplace_back(JsonValue{ std::move(value) });
        }
        root.emplace_back("link_debug_map", JsonValue{ std::move(values) });
    }

    return JsonValue{ std::move(root) };
}
