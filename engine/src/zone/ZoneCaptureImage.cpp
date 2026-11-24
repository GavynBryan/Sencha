#include <zone/ZoneCaptureImage.h>

#include <ecs/World.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/EntityPatch.h>
#include <world/serialization/IComponentSerializer.h>

#include <algorithm>
#include <cstring>

ZoneCaptureImage::Row ZoneCaptureImage::RowAt(std::uint32_t index) const
{
    Row row;
    std::memcpy(&row, Data.data() + index * sizeof(Row), sizeof(Row));
    return row;
}

ZoneCaptureImager::ZoneCaptureImager(const World& world, const ComponentSerializerRegistry& serializers)
    : Entities(world)
    , Serializers(serializers)
{
    for (const auto& serializer : Serializers.Entries())
        if (serializer->Persistence() != ComponentPersistence::Identity)
            if (const ComponentId column = Entities.GetComponentIdByType(serializer->TypeId()); column != InvalidComponentId)
                SerializedColumns.set(column);
}

const ZoneCaptureImager::Written& ZoneCaptureImager::Of(ComponentTypeId type)
{
    const auto [found, inserted] = ByType.try_emplace(type.Value);
    if (inserted)
    {
        Written& written = found->second;
        written.Serializer = Serializers.FindByType(type);
        const ComponentId column = Entities.GetComponentIdByType(type);
        if (column != InvalidComponentId)
        {
            written.Column = column;
            const ComponentMeta* meta = Entities.GetMeta(column);
            written.Size = meta != nullptr && !meta->IsTag ? static_cast<std::uint32_t>(meta->Size) : 0;
        }
    }
    return found->second;
}

const std::vector<ZoneCaptureImager::Written>& ZoneCaptureImager::Carried(std::uint32_t archetype, bool authored)
{
    const auto [found, inserted] = ByArchetype.try_emplace((std::uint64_t{ archetype } << 1) | (authored ? 1u : 0u));
    if (inserted)
    {
        const ArchetypeSignature& signature = Entities.GetArchetypes()[archetype]->Signature;
        for (const auto& serializer : Serializers.Entries())
        {
            const ComponentPersistence persistence = serializer->Persistence();
            if (persistence != ComponentPersistence::Saved && (authored || persistence != ComponentPersistence::Authored))
                continue;
            const Written& written = Of(serializer->TypeId());
            if (written.Column != UINT32_MAX && signature.test(written.Column))
                found->second.push_back(written);
        }
    }
    return found->second;
}

void ZoneCaptureImager::Add(std::vector<Written>& rows, bool& whole, const Written& written) const
{
    if (written.Serializer->SavedFormFromRowAlone())
        rows.push_back(written);
    else
        whole = false;
}

ZoneCaptureImage ZoneCaptureImager::Pack(EntityId entity, std::uint32_t archetype, bool whole,
                                         std::span<const Written> rows) const
{
    ZoneCaptureImage image;
    image.Archetype = archetype;
    image.Whole = whole;
    image.RowCount = static_cast<std::uint32_t>(rows.size());
    std::size_t size = rows.size() * sizeof(ZoneCaptureImage::Row);
    for (const Written& written : rows)
        size += written.Size;
    image.Data.resize(size);
    std::byte* at = image.Data.data();
    for (const Written& written : rows)
    {
        const ZoneCaptureImage::Row row{ written.Serializer->TypeId(), written.Column, written.Size };
        std::memcpy(at, &row, sizeof(row));
        at += sizeof(row);
    }
    for (const Written& written : rows)
        if (written.Size != 0)
        {
            std::memcpy(at, Entities.GetComponentRaw(entity, written.Column), written.Size);
            at += written.Size;
        }
    return image;
}

ZoneCaptureImage ZoneCaptureImager::Authored(EntityId entity, std::span<const AuthoredComponent> baseline)
{
    const std::uint32_t archetype = Entities.ArchetypeOf(entity);
    if (archetype == UINT32_MAX)
        return {};
    Rows.clear();
    bool whole = true;
    const ArchetypeSignature& signature = Entities.GetArchetypes()[archetype]->Signature;
    for (const AuthoredComponent& component : baseline)
    {
        if (!component.Diffed)
            continue;
        const Written& written = Of(component.Type);
        if (written.Serializer != nullptr && written.Column != UINT32_MAX && signature.test(written.Column))
            Add(Rows, whole, written);
    }
    for (const Written& written : Carried(archetype, true))
        if (std::ranges::none_of(baseline, [&](const AuthoredComponent& component) {
                return component.Type == written.Serializer->TypeId();
            }))
            Add(Rows, whole, written);
    return Pack(entity, archetype, whole, Rows);
}

ZoneCaptureImage ZoneCaptureImager::Gained(EntityId entity)
{
    const std::uint32_t archetype = Entities.ArchetypeOf(entity);
    if (archetype == UINT32_MAX)
        return {};
    Rows.clear();
    bool whole = true;
    for (const Written& written : Carried(archetype, false))
        Add(Rows, whole, written);
    return Pack(entity, archetype, whole, Rows);
}

bool ZoneCaptureImager::SameShape(EntityId entity, ZoneCaptureImage& image)
{
    const std::uint32_t archetype = Entities.ArchetypeOf(entity);
    if (image.Archetype == archetype)
        return true;
    if (image.Archetype == UINT32_MAX || archetype == UINT32_MAX)
        return false;
    const auto serialized = [&](std::uint32_t of) -> const ArchetypeSignature& {
        const auto [found, inserted] = SerializedByArchetype.try_emplace(of);
        if (inserted)
            found->second = Entities.GetArchetypes()[of]->Signature & SerializedColumns;
        return found->second;
    };
    if (serialized(image.Archetype) != serialized(archetype))
        return false;
    image.Archetype = archetype;
    return true;
}

bool ZoneCaptureImager::RowsEqual(EntityId entity, const ZoneCaptureImage& image, std::vector<ComponentTypeId>* out) const
{
    bool equal = true;
    const std::byte* bytes = image.Bytes();
    for (std::uint32_t index = 0; index < image.RowCount; ++index)
    {
        const ZoneCaptureImage::Row row = image.RowAt(index);
        const bool same = row.Size == 0 || std::memcmp(Entities.GetComponentRaw(entity, row.Column), bytes, row.Size) == 0;
        bytes += row.Size;
        if (same && out != nullptr)
            out->push_back(row.Type);
        equal = equal && same;
        if (!equal && out == nullptr)
            return false;
    }
    return equal;
}

bool ZoneCaptureImager::Unchanged(EntityId entity, ZoneCaptureImage& image)
{
    return image.Whole && SameShape(entity, image) && RowsEqual(entity, image, nullptr);
}

void ZoneCaptureImager::UnchangedRows(EntityId entity, ZoneCaptureImage& image, std::vector<ComponentTypeId>& out)
{
    if (SameShape(entity, image))
        (void)RowsEqual(entity, image, &out);
}
