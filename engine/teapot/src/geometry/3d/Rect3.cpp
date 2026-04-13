#include <geometry/3d/Rect3.h>

#include <ostream>

Rect3::Rect3(const Vec3& position, const Vec3& size)
	: Position(position), Size(size)
{
}

Rect3::Rect3(float x, float y, float z, float width, float height, float depth)
	: Position(x, y, z), Size(width, height, depth)
{
}

bool Rect3::IsValid() const
{
	return Size.X >= 0.0f && Size.Y >= 0.0f && Size.Z >= 0.0f;
}

Vec3 Rect3::Min() const { return Position; }
Vec3 Rect3::Max() const { return Position + Size; }
Vec3 Rect3::Center() const { return Position + Size / 2.0f; }
float Rect3::Width() const { return Size.X; }
float Rect3::Height() const { return Size.Y; }
float Rect3::Depth() const { return Size.Z; }
float Rect3::Volume() const { return Size.X * Size.Y * Size.Z; }

bool Rect3::Contains(const Vec3& point) const
{
	const Vec3 max = Max();
	return point.X >= Position.X && point.X <= max.X
		&& point.Y >= Position.Y && point.Y <= max.Y
		&& point.Z >= Position.Z && point.Z <= max.Z;
}

bool Rect3::Intersects(const Rect3& other) const
{
	const Vec3 maxA = Max();
	const Vec3 maxB = other.Max();
	return Position.X <= maxB.X && maxA.X >= other.Position.X
		&& Position.Y <= maxB.Y && maxA.Y >= other.Position.Y
		&& Position.Z <= maxB.Z && maxA.Z >= other.Position.Z;
}

bool Rect3::operator==(const Rect3& other) const
{
	return Position == other.Position && Size == other.Size;
}

Rect3 Rect3::FromMinMax(const Vec3& min, const Vec3& max)
{
	return Rect3(min, max - min);
}

Rect3 Rect3::FromCenterSize(const Vec3& center, const Vec3& size)
{
	return Rect3(center - size / 2.0f, size);
}

std::ostream& operator<<(std::ostream& os, const Rect3& rect)
{
	os << "{Position: " << rect.Position << ", Size: " << rect.Size << "}";
	return os;
}
