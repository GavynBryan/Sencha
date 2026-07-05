#include <zone/WorldPartitionManifest.h>

#include <core/json/JsonValue.h>

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

bool ReadBounds(const JsonValue& zone, Aabb3d& out, std::string* error, size_t index)
{
    const JsonValue* value = zone.Find("bounds");
    if (!value)
    {
        SetError(error, std::format("zones[{}].bounds is missing", index));
        return false;
    }
    const JsonValue* min = value->Find("min");
    const JsonValue* max = value->Find("max");
    if (!value->IsObject() || !min || !max || !ReadVec3(*min, out.Min) || !ReadVec3(*max, out.Max))
    {
        SetError(error, std::format("zones[{}].bounds is malformed", index));
        return false;
    }
    return true;
}

const char* TopologyToString(TransitionTopology topology)
{
    switch (topology)
    {
    case TransitionTopology::Seam:     return "seam";
    case TransitionTopology::Doorway:  return "doorway";
    case TransitionTopology::Teleport: return "teleport";
    }
    return "doorway";
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
    if (version->AsNumber() != 1.0)
    {
        SetError(error, std::format("unsupported format_version {}", version->AsNumber()));
        return std::nullopt;
    }

    WorldPartitionManifest manifest;

    if (!ReadOptionalString(root, "name", manifest.Name, error, "name"))
        return std::nullopt;
    if (!ReadOptionalId(root, "start_zone", ZoneIdFromString, manifest.StartZone, error, "start_zone"))
        return std::nullopt;

    if (const JsonValue* regions = root.Find("regions"))
    {
        if (!regions->IsArray())
        {
            SetError(error, "regions must be an array");
            return std::nullopt;
        }
        for (size_t i = 0; i < regions->Size(); ++i)
        {
            const JsonValue& entry = regions->AsArray()[i];
            RegionRecord record;
            if (!ReadRequiredId(entry, "id", RegionIdFromString, record.Id, error,
                                std::format("regions[{}].id", i)))
                return std::nullopt;
            if (!ReadOptionalString(entry, "name", record.Name, error,
                                    std::format("regions[{}].name", i)))
                return std::nullopt;
            manifest.Regions.push_back(std::move(record));
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
            if (!ReadOptionalId(entry, "region", RegionIdFromString, header.Region, error,
                                std::format("zones[{}].region", i)))
                return std::nullopt;
            if (!ReadOptionalString(entry, "scene", header.SceneRef, error,
                                    std::format("zones[{}].scene", i)))
                return std::nullopt;
            if (!ReadBounds(entry, header.Bounds, error, i))
                return std::nullopt;
            if (!ReadOptionalBool(entry, "bounds_overridden", header.BoundsOverridden, error,
                                  std::format("zones[{}].bounds_overridden", i)))
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
            if (const JsonValue* tags = entry.Find("required_tags"))
            {
                if (!tags->IsArray())
                {
                    SetError(error, std::format("transitions[{}].required_tags must be an array",
                                                i));
                    return std::nullopt;
                }
                for (const JsonValue& tag : tags->AsArray())
                {
                    if (!tag.IsString() || tag.AsString().empty())
                    {
                        SetError(error,
                                 std::format("transitions[{}].required_tags entries must be "
                                             "nonempty strings",
                                             i));
                        return std::nullopt;
                    }
                    record.RequiredTags.push_back(tag.AsString());
                }
            }
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
            if (const JsonValue* priority = entry.Find("preload_priority"))
            {
                const double number = priority->IsNumber()
                    ? priority->AsNumber()
                    : std::numeric_limits<double>::quiet_NaN();
                if (!std::isfinite(number) || number != std::floor(number)
                    || number < std::numeric_limits<int32_t>::min()
                    || number > std::numeric_limits<int32_t>::max())
                {
                    SetError(error, std::format("transitions[{}].preload_priority must be a 32-bit integer", i));
                    return std::nullopt;
                }
                record.PreloadPriority = static_cast<int32_t>(number);
            }
            if (const JsonValue* depth = entry.Find("preload_depth"))
            {
                const double number = depth->IsNumber()
                    ? depth->AsNumber()
                    : std::numeric_limits<double>::quiet_NaN();
                if (!std::isfinite(number) || number != std::floor(number) || number < 0
                    || number > std::numeric_limits<int32_t>::max())
                {
                    SetError(error, std::format(
                        "transitions[{}].preload_depth must be a non-negative integer", i));
                    return std::nullopt;
                }
                record.PreloadDepth = static_cast<int32_t>(number);
            }
            manifest.Transitions.push_back(record);
        }
    }

    return manifest;
}

JsonValue WriteWorldPartitionManifest(const WorldPartitionManifest& manifest)
{
    JsonValue::Object root;
    root.emplace_back("format_version", JsonValue{ 1 });
    root.emplace_back("name", JsonValue{ manifest.Name });
    if (manifest.StartZone.IsValid())
        root.emplace_back("start_zone", JsonValue{ ZoneIdToString(manifest.StartZone) });

    JsonValue::Array regions;
    for (const RegionRecord& record : manifest.Regions)
    {
        JsonValue::Object entry;
        entry.emplace_back("id", JsonValue{ RegionIdToString(record.Id) });
        entry.emplace_back("name", JsonValue{ record.Name });
        regions.emplace_back(JsonValue{ std::move(entry) });
    }
    root.emplace_back("regions", JsonValue{ std::move(regions) });

    JsonValue::Array zones;
    for (const ZoneHeader& header : manifest.Zones)
    {
        JsonValue::Object entry;
        entry.emplace_back("id", JsonValue{ ZoneIdToString(header.Id) });
        entry.emplace_back("name", JsonValue{ header.Name });
        if (header.Region.IsValid())
            entry.emplace_back("region", JsonValue{ RegionIdToString(header.Region) });
        entry.emplace_back("scene", JsonValue{ header.SceneRef });

        JsonValue::Object bounds;
        bounds.emplace_back("min", WriteVec3(header.Bounds.Min));
        bounds.emplace_back("max", WriteVec3(header.Bounds.Max));
        entry.emplace_back("bounds", JsonValue{ std::move(bounds) });

        entry.emplace_back("bounds_overridden", JsonValue{ header.BoundsOverridden });

        if (!header.CookedSceneRef.empty())
            entry.emplace_back("cooked_scene", JsonValue{ header.CookedSceneRef });
        if (!header.CookedCollisionRef.empty())
            entry.emplace_back("cooked_collision", JsonValue{ header.CookedCollisionRef });
        if (header.CookedContentHash != 0)
            entry.emplace_back("content_hash", JsonValue{ HexEncode(header.CookedContentHash) });

        zones.emplace_back(JsonValue{ std::move(entry) });
    }
    root.emplace_back("zones", JsonValue{ std::move(zones) });

    JsonValue::Array transitions;
    for (const TransitionRecord& record : manifest.Transitions)
    {
        JsonValue::Object entry;
        entry.emplace_back("id", JsonValue{ TransitionIdToString(record.Id) });
        if (!record.Name.empty())
            entry.emplace_back("name", JsonValue{ record.Name });
        entry.emplace_back("from", JsonValue{ ZoneIdToString(record.From) });
        entry.emplace_back("to", JsonValue{ ZoneIdToString(record.To) });
        entry.emplace_back("topology", JsonValue{ TopologyToString(record.Topology) });
        entry.emplace_back("one_way", JsonValue{ record.Flags.OneWay });
        entry.emplace_back("preload_priority", JsonValue{ record.PreloadPriority });
        if (record.PreloadDepth != 0)
            entry.emplace_back("preload_depth", JsonValue{ record.PreloadDepth });
        if (!record.RequiredTags.empty())
        {
            JsonValue::Array tags;
            for (const std::string& tag : record.RequiredTags)
                tags.emplace_back(JsonValue{ tag });
            entry.emplace_back("required_tags", JsonValue{ std::move(tags) });
        }
        transitions.emplace_back(JsonValue{ std::move(entry) });
    }
    root.emplace_back("transitions", JsonValue{ std::move(transitions) });

    return JsonValue{ std::move(root) };
}
