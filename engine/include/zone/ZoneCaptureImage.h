#pragma once

#include <ecs/ArchetypeSignature.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

class ComponentSerializerRegistry;
struct AuthoredComponent;
struct IComponentSerializer;
class World;

// An entity's rows as its zone's last capture read them: docs/gameplay/saves.md, "Capture at scale".
struct ZoneCaptureImage
{
    struct Row
    {
        ComponentTypeId Type;
        std::uint32_t Column = 0;
        std::uint32_t Size = 0;
    };

    std::uint32_t Archetype = UINT32_MAX;
    // Every component the capture writes for the entity has a row, so equal rows leave its last result standing.
    bool Whole = false;
    std::uint32_t RowCount = 0;
    // RowCount rows, then their bytes in row order: one allocation per entity.
    std::vector<std::byte> Data;

    [[nodiscard]] Row RowAt(std::uint32_t index) const;
    [[nodiscard]] const std::byte* Bytes() const { return Data.data() + RowCount * sizeof(Row); }
};

// Images entities for one capture or import, remembering per archetype what a capture writes of it.
class ZoneCaptureImager
{
public:
    ZoneCaptureImager(const World& world, const ComponentSerializerRegistry& serializers);

    [[nodiscard]] ZoneCaptureImage Authored(EntityId entity, std::span<const AuthoredComponent> baseline);
    [[nodiscard]] ZoneCaptureImage Gained(EntityId entity);

    // Whole, carrying the same serialized components, and every row equal.
    [[nodiscard]] bool Unchanged(EntityId entity, ZoneCaptureImage& image);
    // The components whose rows are equal; none when the serialized components it carries differ.
    void UnchangedRows(EntityId entity, ZoneCaptureImage& image, std::vector<ComponentTypeId>& out);

private:
    struct Written
    {
        const IComponentSerializer* Serializer = nullptr;
        std::uint32_t Column = UINT32_MAX;
        std::uint32_t Size = 0;
    };

    [[nodiscard]] const Written& Of(ComponentTypeId type);
    [[nodiscard]] const std::vector<Written>& Carried(std::uint32_t archetype, bool authored);
    void Add(std::vector<Written>& rows, bool& whole, const Written& written) const;
    [[nodiscard]] ZoneCaptureImage Pack(EntityId entity, std::uint32_t archetype, bool whole,
                                        std::span<const Written> rows) const;
    [[nodiscard]] bool SameShape(EntityId entity, ZoneCaptureImage& image);
    [[nodiscard]] bool RowsEqual(EntityId entity, const ZoneCaptureImage& image, std::vector<ComponentTypeId>* out) const;

    const World& Entities;
    const ComponentSerializerRegistry& Serializers;
    std::unordered_map<std::uint64_t, Written> ByType;
    std::unordered_map<std::uint64_t, std::vector<Written>> ByArchetype;
    ArchetypeSignature SerializedColumns;
    std::unordered_map<std::uint32_t, ArchetypeSignature> SerializedByArchetype;
    std::vector<Written> Rows;
};
