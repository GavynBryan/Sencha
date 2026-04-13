#include <math/geometry/3d/Rect3d.h>

#include <ostream>

Rect3d::Rect3d(const Vec3d& position, const Vec3d& size)
	: Position(position), Size(size)
{
}

Rect3d::Rect3d(float x, float y, float z, float width, float height, float depth)
	: Position(x, y, z), Size(width, height, depth)
{
}

bool Rect3d::IsValid() const
{
	return Size.X >= 0.0f && Size.Y >= 0.0f && Size.Z >= 0.0f;
}

Vec3d Rect3d::Min() const { return Position; }
Vec3d Rect3d::Max() const { return Position + Size; }
Vec3d Rect3d::Center() const { return Position + Size / 2.0f; }
float Rect3d::Width() const { return Size.X; }
float Rect3d::Height() const { return Size.Y; }
float Rect3d::Depth() const { return Size.Z; }
float Rect3d::Volume() const { return Size.X * Size.Y * Size.Z; }

bool Rect3d::Contains(const Vec3d& point) const
{
	const Vec3d max = Max();
	return point.X >= Position.X && point.X <= max.X
		&& point.Y >= Position.Y && point.Y <= max.Y
		&& point.Z >= Position.Z && point.Z <= max.Z;
}

bool Rect3d::Intersects(const Rect3d& other) const
{
	const Vec3d maxA = Max();
	const Vec3d maxB = other.Max();
	return Position.X <= maxB.X && maxA.X >= other.Position.X
		&& Position.Y <= maxB.Y && maxA.Y >= other.Position.Y
		&& Position.Z <= maxB.Z && maxA.Z >= other.Position.Z;
}

bool Rect3d::operator==(const Rect3d& other) const
{
	return Position == other.Position && Size == other.Size;
}

Rect3d Rect3d::FromMinMax(const Vec3d& min, const Vec3d& max)
{
	return Rect3d(min, max - min);
}

Rect3d Rect3d::FromCenterSize(const Vec3d& center, const Vec3d& size)
{
	return Rect3d(center - size / 2.0f, size);
}

std::ostream& operator<<(std::ostream& os, const Rect3d& rect)
{
	os << "{Position: " << rect.Position << ", Size: " << rect.Size << "}";
	return os;
}
