#include <physics/CollisionShapeCache.h>

#include "CollisionShapeCacheImpl.h"
#include "JoltStartup.h"

#include <sstream>
#include <string>

#include <Jolt/Core/StreamWrapper.h>

CollisionShapeCache::CollisionShapeCache()
    : Impl(std::make_unique<CollisionShapeCacheImpl>())
{
}

CollisionShapeCache::~CollisionShapeCache() = default;
CollisionShapeCache::CollisionShapeCache(CollisionShapeCache&&) noexcept = default;
CollisionShapeCache& CollisionShapeCache::operator=(CollisionShapeCache&&) noexcept = default;

CollisionShapeHandle CollisionShapeCache::LoadBlob(std::span<const std::byte> blob)
{
    EnsureJoltRegistered(); // sRestoreFromBinaryState builds the shape via the Factory

    std::string buffer(reinterpret_cast<const char*>(blob.data()), blob.size());
    std::istringstream stream(buffer, std::ios::binary);
    JPH::StreamInWrapper in(stream);

    JPH::Shape::ShapeResult result = JPH::Shape::sRestoreFromBinaryState(in);
    if (result.HasError() || !result.IsValid())
        return CollisionShapeHandle{};

    Impl->Shapes.push_back(result.Get());
    return CollisionShapeHandle{ static_cast<uint32_t>(Impl->Shapes.size()) };
}

bool CollisionShapeCache::Has(CollisionShapeHandle handle) const
{
    return handle.Value != 0 && handle.Value <= Impl->Shapes.size();
}

std::size_t CollisionShapeCache::Count() const
{
    return Impl->Shapes.size();
}
