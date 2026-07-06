#include "GltfMeshExport.h"

#include <core/json/JsonStringify.h>
#include <core/json/JsonValue.h>
#include <render/static_mesh/StaticMeshVertex.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <vector>

namespace
{
constexpr uint32_t kGlbMagic = 0x46546C67;   // "glTF"
constexpr uint32_t kGlbVersion = 2;
constexpr uint32_t kChunkJson = 0x4E4F534A;  // "JSON"
constexpr uint32_t kChunkBin = 0x004E4942;   // "BIN"

constexpr int kComponentFloat = 5126;
constexpr int kComponentUint32 = 5125;
constexpr int kTargetArrayBuffer = 34962;
constexpr int kTargetElementArrayBuffer = 34963;

// A material stub's display name: the path stem reads well in a DCC's slot
// list ("asset://materials/dev/gray.smat" -> "gray").
std::string MaterialName(const AssetRef& material, std::size_t index)
{
    if (material.Path.empty())
        return "material_" + std::to_string(index);
    const std::filesystem::path path(material.Path);
    const std::string stem = path.stem().string();
    return stem.empty() ? material.Path : stem;
}
}

bool WriteGlbFile(const MeshGeometry& geometry,
                  std::span<const AssetRef> materialOrder,
                  const std::filesystem::path& path,
                  std::string* error)
{
    if (geometry.Vertices.empty() || geometry.Indices.empty() || geometry.Sections.empty())
    {
        if (error != nullptr)
            *error = "no geometry to export";
        return false;
    }

    // BIN chunk: interleaved vertices then indices, each 4-byte aligned.
    const std::size_t vertexBytes = geometry.Vertices.size() * sizeof(StaticMeshVertex);
    const std::size_t indexBytes = geometry.Indices.size() * sizeof(uint32_t);
    std::vector<std::byte> bin(vertexBytes + indexBytes);
    std::memcpy(bin.data(), geometry.Vertices.data(), vertexBytes);
    std::memcpy(bin.data() + vertexBytes, geometry.Indices.data(), indexBytes);
    while (bin.size() % 4 != 0)
        bin.push_back(std::byte{ 0 });

    // POSITION accessor min/max (required by the format).
    Vec3d minPos{ std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                  std::numeric_limits<float>::max() };
    Vec3d maxPos{ std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
                  std::numeric_limits<float>::lowest() };
    for (const StaticMeshVertex& v : geometry.Vertices)
    {
        minPos.X = std::min(minPos.X, v.Position.X);
        minPos.Y = std::min(minPos.Y, v.Position.Y);
        minPos.Z = std::min(minPos.Z, v.Position.Z);
        maxPos.X = std::max(maxPos.X, v.Position.X);
        maxPos.Y = std::max(maxPos.Y, v.Position.Y);
        maxPos.Z = std::max(maxPos.Z, v.Position.Z);
    }

    const auto vec3Json = [](Vec3d v)
    { return JsonValue(JsonValue::Array{ JsonValue(double(v.X)), JsonValue(double(v.Y)), JsonValue(double(v.Z)) }); };
    const auto obj = [](JsonValue::Object o) { return JsonValue(std::move(o)); };

    // Buffer views: 0 = interleaved vertex stream (stride 48), 1 = index stream.
    JsonValue::Array bufferViews;
    bufferViews.push_back(obj({ { "buffer", JsonValue(0) },
                                { "byteOffset", JsonValue(0) },
                                { "byteLength", JsonValue(double(vertexBytes)) },
                                { "byteStride", JsonValue(int(sizeof(StaticMeshVertex))) },
                                { "target", JsonValue(kTargetArrayBuffer) } }));
    bufferViews.push_back(obj({ { "buffer", JsonValue(0) },
                                { "byteOffset", JsonValue(double(vertexBytes)) },
                                { "byteLength", JsonValue(double(indexBytes)) },
                                { "target", JsonValue(kTargetElementArrayBuffer) } }));

    // Accessors 0-3: the shared vertex attributes; 4+: one index accessor per
    // section. Attribute byte offsets follow the StaticMeshVertex layout.
    const int vertexCount = static_cast<int>(geometry.Vertices.size());
    JsonValue::Array accessors;
    accessors.push_back(obj({ { "bufferView", JsonValue(0) },
                              { "byteOffset", JsonValue(int(offsetof(StaticMeshVertex, Position))) },
                              { "componentType", JsonValue(kComponentFloat) },
                              { "count", JsonValue(vertexCount) },
                              { "type", JsonValue("VEC3") },
                              { "min", vec3Json(minPos) },
                              { "max", vec3Json(maxPos) } }));
    accessors.push_back(obj({ { "bufferView", JsonValue(0) },
                              { "byteOffset", JsonValue(int(offsetof(StaticMeshVertex, Normal))) },
                              { "componentType", JsonValue(kComponentFloat) },
                              { "count", JsonValue(vertexCount) },
                              { "type", JsonValue("VEC3") } }));
    accessors.push_back(obj({ { "bufferView", JsonValue(0) },
                              { "byteOffset", JsonValue(int(offsetof(StaticMeshVertex, Uv0))) },
                              { "componentType", JsonValue(kComponentFloat) },
                              { "count", JsonValue(vertexCount) },
                              { "type", JsonValue("VEC2") } }));
    accessors.push_back(obj({ { "bufferView", JsonValue(0) },
                              { "byteOffset", JsonValue(int(offsetof(StaticMeshVertex, Tangent))) },
                              { "componentType", JsonValue(kComponentFloat) },
                              { "count", JsonValue(vertexCount) },
                              { "type", JsonValue("VEC4") } }));

    JsonValue::Array primitives;
    for (const StaticMeshSection& section : geometry.Sections)
    {
        const int indexAccessor = static_cast<int>(accessors.size());
        accessors.push_back(obj({ { "bufferView", JsonValue(1) },
                                  { "byteOffset", JsonValue(double(section.IndexOffset * sizeof(uint32_t))) },
                                  { "componentType", JsonValue(kComponentUint32) },
                                  { "count", JsonValue(double(section.IndexCount)) },
                                  { "type", JsonValue("SCALAR") } }));
        JsonValue::Object primitive{
            { "attributes", obj({ { "POSITION", JsonValue(0) },
                                  { "NORMAL", JsonValue(1) },
                                  { "TEXCOORD_0", JsonValue(2) },
                                  { "TANGENT", JsonValue(3) } }) },
            { "indices", JsonValue(indexAccessor) },
        };
        if (section.MaterialSlot < materialOrder.size())
            primitive.emplace_back("material", JsonValue(int(section.MaterialSlot)));
        primitives.push_back(obj(std::move(primitive)));
    }

    JsonValue::Array materials;
    for (std::size_t i = 0; i < materialOrder.size(); ++i)
        materials.push_back(obj({ { "name", JsonValue(MaterialName(materialOrder[i], i)) } }));

    JsonValue::Object root{
        { "asset", obj({ { "version", JsonValue("2.0") }, { "generator", JsonValue("sencha_editor") } }) },
        { "buffers", JsonValue(JsonValue::Array{ obj({ { "byteLength", JsonValue(double(bin.size())) } }) }) },
        { "bufferViews", JsonValue(std::move(bufferViews)) },
        { "accessors", JsonValue(std::move(accessors)) },
        { "meshes", JsonValue(JsonValue::Array{ obj({ { "primitives", JsonValue(std::move(primitives)) } }) }) },
        { "nodes", JsonValue(JsonValue::Array{ obj({ { "mesh", JsonValue(0) } }) }) },
        { "scenes", JsonValue(JsonValue::Array{ obj({ { "nodes", JsonValue(JsonValue::Array{ JsonValue(0) }) } }) }) },
        { "scene", JsonValue(0) },
    };
    if (!materials.empty())
        root.emplace_back("materials", JsonValue(std::move(materials)));

    std::string json = JsonStringify(JsonValue(std::move(root)));
    while (json.size() % 4 != 0)
        json.push_back(' '); // the format pads the JSON chunk with spaces

    const uint32_t jsonLength = static_cast<uint32_t>(json.size());
    const uint32_t binLength = static_cast<uint32_t>(bin.size());
    const uint32_t totalLength = 12 + 8 + jsonLength + 8 + binLength;

    std::ofstream file(path, std::ios::binary);
    if (!file.is_open())
    {
        if (error != nullptr)
            *error = "cannot open '" + path.string() + "' for writing";
        return false;
    }

    const auto writeU32 = [&](uint32_t value) { file.write(reinterpret_cast<const char*>(&value), 4); };
    writeU32(kGlbMagic);
    writeU32(kGlbVersion);
    writeU32(totalLength);
    writeU32(jsonLength);
    writeU32(kChunkJson);
    file.write(json.data(), static_cast<std::streamsize>(json.size()));
    writeU32(binLength);
    writeU32(kChunkBin);
    file.write(reinterpret_cast<const char*>(bin.data()), static_cast<std::streamsize>(bin.size()));

    if (!file.good())
    {
        if (error != nullptr)
            *error = "write to '" + path.string() + "' failed";
        return false;
    }
    return true;
}
