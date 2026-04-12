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
	return Size.Data[0] >= 0.0f && Size.Data[1] >= 0.0f && Size.Data[2] >= 0.0f;
}

Vec3 Rect3::Min() const { return Position; }
Vec3 Rect3::Max() const { return Position + Size; }
Vec3 Rect3::Center() const { return Position + Size / 2.0f; }
float Rect3::Width() const { return Size.Data[0]; }
float Rect3::Height() const { return Size.Data[1]; }
float Rect3::Depth() const { return Size.Data[2]; }
float Rect3::Volume() const { return Size.Data[0] * Size.Data[1] * Size.Data[2]; }

bool Rect3::Contains(const Vec3& point) const
{
	const Vec3 max = Max();
	return point.Data[0] >= Position.Data[0] && point.Data[0] <= max.Data[0]
		&& point.Data[1] >= Position.Data[1] && point.Data[1] <= max.Data[1]
		&& point.Data[2] >= Position.Data[2] && point.Data[2] <= max.Data[2];
}

bool Rect3::Intersects(const Rect3& other) const
{
	const Vec3 maxA = Max();
	const Vec3 maxB = other.Max();
	return Position.Data[0] <= maxB.Data[0] && maxA.Data[0] >= other.Position.Data[0]
		&& Position.Data[1] <= maxB.Data[1] && maxA.Data[1] >= other.Position.Data[1]
		&& Position.Data[2] <= maxB.Data[2] && maxA.Data[2] >= other.Position.Data[2];
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
